#include "easy_bf3.hpp"
#include <loguru.hpp>
#include "../pempek_assert.hpp"
#include "../batsched_tools.hpp"
using namespace std;
// @note LH: testing macros


EasyBackfilling3::EasyBackfilling3(Workload * workload,
                                 SchedulingDecision * decision,
                                 Queue * queue,
                                 ResourceSelector * selector,
                                 double rjms_delay,
                                 rapidjson::Document * variant_options) :
    ISchedulingAlgorithm(workload, decision, queue, selector, rjms_delay, variant_options)
{
        //initialize reservation queue
    SortableJobOrder * order = new FCFSOrder;//reservations do not get killed so we do not need OriginalFCFSOrder for this
    _reservation_queue = new Queue(order);
    // @note LH: test csv file
    _testCSV = new b_log();
    _p_job = new Priority_Job();
}

EasyBackfilling3::~EasyBackfilling3()
{

}

void EasyBackfilling3::on_simulation_start(double date, const rapidjson::Value & batsim_event)
{
    // @note LH added for time analysis
    GET_TIME(_begin_overall);
    LOG_F(INFO,"on simulation start");
    pid_t pid = batsched_tools::get_batsched_pid();
    _decision->add_generic_notification("PID",std::to_string(pid),date);
    const rapidjson::Value & batsim_config = batsim_event["config"];

    _output_folder=batsim_config["output-folder"].GetString();
    _output_folder.replace(_output_folder.rfind("/out"), std::string("/out").size(), "");
    LOG_F(INFO,"output folder %s",_output_folder.c_str());

    // @note LH: create csv file for analyzing end time diffs
    _testCSV->add_log_file(_output_folder+"/out_endtime_diffs.csv",b_log::CSV);
    string header_fmt = "JOB_ID,START_TIME,RUN_TIME,EST_END_TIME,REAL_END_TIME";
    TCSV_F(b_log::CSV, date, "%s", header_fmt.c_str());
    //was there
    ISchedulingAlgorithm::set_generators(date);
    
    _available_machines.insert(IntervalSet::ClosedInterval(0, _nb_machines - 1));
    _nb_available_machines = _nb_machines;
    PPK_ASSERT_ERROR(_available_machines.size() == (unsigned int) _nb_machines);
    (void) batsim_config;
}

void EasyBackfilling3::on_simulation_end(double date)
{
    // @note LH added for time analysis
    GET_TIME(_end_overall);
    _overall_time = _end_overall-_begin_overall;
    //  @note show total backfilled jobs
    LOG_F(ERROR, "[Overall_Time] = %.15f, [Decision_Time] = %.15f, [Backfilled_Jobs] = %d", _overall_time, _decision_time, _backfill_counter);
    (void) date;
}


void EasyBackfilling3::make_decisions(double date,
                                     SortableJobOrder::UpdateInformation *update_info,
                                     SortableJobOrder::CompareInformation *compare_info)
{
    // @note LH added for time analysis
    GET_TIME(_begin_decision);
    const Job * priority_job_before = _queue->first_job_or_nullptr();

    // Let's remove finished jobs from the schedule
    for (const string & ended_job_id : _jobs_ended_recently){

        // @note LH: get finsihed jobs from scheduler
        auto j_iter = std::find_if(_scheduled_jobs.begin(), _scheduled_jobs.end(), [&](Scheduled_Job *sj) { 
            return (sj->id == ended_job_id);
        });

        // @note LH: check if finished job exists
        if (j_iter != _scheduled_jobs.end()){
            auto j_index = distance(_scheduled_jobs.begin(), j_iter);
            _tmp_job = _scheduled_jobs.at(j_index);

            // @note LH: output csv row of finsihed job data
            auto row_str = batsched_tools::string_format(
                "%s,%.15f,%.15f,%.15f,%.15f",
                _tmp_job->id.c_str(),
                _tmp_job->start_time,
                _tmp_job->run_time,
                _tmp_job->est_finish_time,
                date // real finish time
            );
            TCSV_F(b_log::CSV, date, "%s", row_str.c_str());

            // @note LH: put back the finished jobs used machines
            _available_machines.insert(_tmp_job->allocated_machines);
            _nb_available_machines += _tmp_job->requested_resources;

            // @note LH: remove the finished job 
            _scheduled_jobs.erase(j_iter);
        }
        // @note reset tmp job 
        _tmp_job = NULL;
    }
    
    // Let's handle recently released jobs
    std::vector<std::string> recently_queued_jobs;
    for (const string & new_job_id : _jobs_released_recently)
    {
        const Job * new_job = (*_workload)[new_job_id];

        if (new_job->nb_requested_resources > _nb_machines)
        {
            _decision->add_reject_job(new_job_id, date);
        }
        else if (!new_job->has_walltime)
        {
            LOG_SCOPE_FUNCTION(INFO);
            LOG_F(INFO, "Date=%g. Rejecting job '%s' as it has no walltime", date, new_job_id.c_str());
            _decision->add_reject_job(new_job_id, date);
        }
        else
        {
            _queue->append_job(new_job, update_info);
            recently_queued_jobs.push_back(new_job_id);
        }
    }


    // Queue sorting
    const Job * priority_job_after = nullptr;
    sort_queue_while_handling_priority_job(priority_job_before, priority_job_after, update_info, compare_info);

    // If no resources have been released, we can just try to backfill the newly-released jobs
    if (_jobs_ended_recently.empty())
    {

        // @note LH: priority jobs do not run here (hence loops if condition)
        _is_priority = false;

        for (unsigned int i = 0; i < recently_queued_jobs.size() && _nb_available_machines > 0; ++i)
        {
            const string & new_job_id = recently_queued_jobs[i];
            const Job * new_job = (*_workload)[new_job_id];

            // The job could have already been executed by sort_queue_while_handling_priority_job,
            // that's why we check whether the queue contains the job.
            if (_queue->contains_job(new_job) 
                && new_job != priority_job_after)
            {
                
                check_next_job(new_job, date); 
                if(_can_run){
                    _decision->add_execute_job(new_job_id, _tmp_job->allocated_machines, date);
                    _queue->remove_job(new_job);
                    _backfill_counter++;
                }
            }
        }
    }
    else
    {
        // Some resources have been released, the whole queue should be traversed.
        auto job_it = _queue->begin();
        int nb_available_machines = _nb_available_machines;
        // Let's try to backfill all the jobs
        while (job_it != _queue->end() && nb_available_machines > 0)
        {
            const Job * job = (*job_it)->job;

            /* @note LH: was in old version will 
                //remove the job if it exists
                remove_scheduled_job(job->id);
            */
            // @note check if job is the priority job
            _is_priority = job == priority_job_after; 

            check_next_job(priority_job_after, update_info->current_date.convert_to<double>());
            if(_can_run){
                _decision->add_execute_job(job->id, _tmp_job->allocated_machines, date);
                job_it = _queue->remove_job(job_it); // Updating job_it to remove on traversal
                if(_is_priority) priority_job_after = _queue->first_job_or_nullptr();
                else _backfill_counter++;
            }else ++job_it;  
        }

    }

    // @note LH: adds queuing info to the out_jobs_extra.csv file
    _decision->add_generic_notification("queue_size",std::to_string(_queue->nb_jobs()),date);
    _decision->add_generic_notification("schedule_size",std::to_string(_scheduled_jobs.size()),date);
    
/*  // @todo LH: add this later with new schedule
    _decision->add_generic_notification("number_running_jobs",std::to_string(_schedule.get_number_of_running_jobs()),date);
    _decision->add_generic_notification("utilization",std::to_string(_schedule.get_utilization()),date);
    _decision->add_generic_notification("utilization_no_resv",std::to_string(_schedule.get_utilization_no_resv()),date);
*/
    // @note LH added for time analysis
    GET_TIME(_end_decision);
    _decision_time += (_end_decision-_begin_decision);
}


void EasyBackfilling3::sort_queue_while_handling_priority_job(const Job * priority_job_before,
                                                             const Job *& priority_job_after,
                                                             SortableJobOrder::UpdateInformation * update_info,
                                                             SortableJobOrder::CompareInformation * compare_info)
{
    // Let's sort the queue
    _queue->sort_queue(update_info, compare_info);
    // Let the new priority job be computed
    priority_job_after = _queue->first_job_or_nullptr();

    // If the priority job has changed
    if (priority_job_after != priority_job_before)
    {
        /* @note LH: was in old version: remove the old priority job
        if (priority_job_before != nullptr)
            remove_scheduled_job(priority_job_before->id);
        */

        // @note LH: only priority jobs run here 
        _is_priority = true; 
        // Let us ensure the priority job is in the schedule.
        // To do so, while the priority job can be executed now, we keep on inserting it into the schedule
        for (bool could_run_priority_job = true; could_run_priority_job && priority_job_after != nullptr; )
        {
            could_run_priority_job = false;
            check_next_job(priority_job_after, update_info->current_date.convert_to<double>());
            if(_can_run){
                _decision->add_execute_job(priority_job_after->id, _tmp_job->allocated_machines, update_info->current_date.convert_to<double>());
                _queue->remove_job(priority_job_after);
                priority_job_after = _queue->first_job_or_nullptr();
                could_run_priority_job = true;
            }
        }
    }
}


//@note LH: added replacement function for time slices
void EasyBackfilling3::check_next_job(const Job * next_job, double date)                                                    
{   
    _tmp_job = NULL;
    int machine_count = _nb_available_machines;

    if(_is_priority){
        /* @note 
            priority job can run if the following is true:
                - requested resources <=  current available 
        */
        _can_run =  next_job->nb_requested_resources <= _nb_available_machines;
        // @note update priority job if it changed
        if(_p_job->id != next_job->id){
            _p_job->id = next_job->id;
            _p_job->requested_resources = next_job->nb_requested_resources;
        }
        // @note if the priority job can't run then calculate when it will 
        if(!_can_run){
            for(auto & sj: _scheduled_jobs){
                machine_count += sj->requested_resources;
                if(machine_count >= _p_job->requested_resources){
                    _p_job->shadow_time = sj->est_finish_time;
                    _p_job->est_finish_time = sj->est_finish_time+next_job->duration;
                    _p_job->extra_resources = machine_count-next_job->nb_requested_resources;
                    break;
                }
            }
        }
    }else{
        /* @note
            job can be backfilled if ONE of the following is true:
            - requested resources <= current available -AND- it will finish before the current priority jobs start (shadow) time
            - requested resources <= MIN[ current available resources, priority job calculated 'extra resources' ]
        */
        _can_run = ((next_job->nb_requested_resources <= _nb_available_machines) && ((date+next_job->duration) <= _p_job->shadow_time)) 
            || next_job->nb_requested_resources <= (MIN(_nb_available_machines,_p_job->extra_resources));
    }
    

    // @note current job can run
    if(_can_run){
        _tmp_job = new Scheduled_Job();
        _tmp_job->id = next_job->id;
        _tmp_job->requested_resources = next_job->nb_requested_resources;
        _tmp_job->start_time = date;
        _tmp_job->wall_time = next_job->walltime.convert_to<double>();
        _tmp_job->run_time = next_job->duration;
        _tmp_job->est_finish_time = date + next_job->duration;
        _tmp_job->allocated_machines = _available_machines.left(next_job->nb_requested_resources);
        // @note add the job to the schedule
        _scheduled_jobs.push_back(_tmp_job);
        // @note remove allocated nodes from intervalset and subtract available nodes 
        _available_machines -= _tmp_job->allocated_machines;
        _nb_available_machines -= _tmp_job->requested_resources;
        // @note sort the schedule incase job was backfilled
        schedule_heap_sort(_scheduled_jobs.size());
    }
    /*
    // @note logging to verify 
    auto res_str = batsched_tools::string_format(
                "check_next_job(): JOB_ID[%s]: is_priority = %s, can_run = %s, requested_resources = %d, _nb_available_machines = %d, _p_job->extra_machines = %d, start_time = %.15f, est_end_time = %.15f\n",
                next_job->id.c_str(),
                _is_priority ? "true" : "false",
                _can_run ? "true" : "false",
                next_job->nb_requested_resources,
                _nb_available_machines,
                _p_job->extra_resources,
                date,
                date+next_job->duration
    );
    LOG_F(ERROR,"%s", res_str.c_str());
    */
}

//@note LH: added function to remove jobs from schedule
void EasyBackfilling3::remove_scheduled_job(string job_id) {

    auto j_iter = find_if(_scheduled_jobs.begin(), _scheduled_jobs.end(), [&](Scheduled_Job *sj) { 
        return (sj->id == job_id);
    });
    if(j_iter != _scheduled_jobs.end()) _scheduled_jobs.erase(j_iter);
}

//@note LH: added helper function to turn schedule into a maximum heap
void EasyBackfilling3::max_heapify(int root, int size) {
    // Find largest among root, left child and right child
    int max = root, left = (2 * root) + 1, right = left + 1;
    if (left < size && _scheduled_jobs[left]->est_finish_time > _scheduled_jobs[max]->est_finish_time)
        max = left;
    if (right < size && _scheduled_jobs[right]->est_finish_time  > _scheduled_jobs[max]->est_finish_time)
        max = right;
    // Swap and continue heapifying if root is not largest
    if (max != root) {
        swap(_scheduled_jobs[root], _scheduled_jobs[max]);
        max_heapify(max, size);
    }
}

//@note LH: added main function for heap sorting the schedule
void EasyBackfilling3::schedule_heap_sort(int size)
{
    for(int i = size / 2; i >= 0; i--)
        max_heapify(i, size);

    for(int i = size - 1; i > 0; i--){
        max_heapify(0, i);
    }
    /*
    // @note LH: logging to verify 
    string fmt = "%s,%.15f,%.15f,%.15f,%.15f,%s";
    LOG_F(ERROR, "------------------------------------------------------------------------");
    for(auto & sj: _scheduled_jobs){
        auto res_str = batsched_tools::string_format(fmt,
                sj->id.c_str(),
                sj->start_time,
                sj->run_time,
                sj->est_finish_time,
                sj->allocated_machines.to_string_hyphen().c_str()
        );
        LOG_F(ERROR, "%s", res_str.c_str());
    }
    LOG_F(ERROR, "------------------------------------------------------------------------");
    */
}
