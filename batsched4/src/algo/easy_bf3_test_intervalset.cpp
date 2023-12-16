#include "easy_bf3.hpp"
#include <loguru.hpp>
#include "../pempek_assert.hpp"
#include "../batsched_tools.hpp"
using namespace std;

// @note LH: testing macros
#define T_CSV_INSTANCE _testCSV

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
}

EasyBackfilling3::~EasyBackfilling3()
{

}

void EasyBackfilling3::on_simulation_start(double date, const rapidjson::Value & batsim_event)
{
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

    // @todo LH: DELETE THIS
    _schedule = Schedule(_nb_machines, date);
    (void) batsim_config;

}

void EasyBackfilling3::on_simulation_end(double date)
{

    LOG_F(INFO,"on simulation end");
    //  @note show total backfilled jobs
    LOG_F(ERROR,"[TOTAL_BACKFILLED_JOBS] = %d", _backfill_counter);
    (void) date;
    
}

// @note func: make_decisions()
void EasyBackfilling3::make_decisions(double date,
                                     SortableJobOrder::UpdateInformation *update_info,
                                     SortableJobOrder::CompareInformation *compare_info)
{
    const Job * priority_job_before = _queue->first_job_or_nullptr();
    // @note LH: added fmt string for output csv
    string row_fmt = "%s,%.15f,%.15f,%.15f,%.15f";

    // Let's remove finished jobs from the schedule
    for (const string & ended_job_id : _jobs_ended_recently){
        _schedule.remove_job((*_workload)[ended_job_id]);
        // @note LH: get finsihed jobs from scheduler
        auto j_iter = std::find_if(_scheduled_jobs.begin(), _scheduled_jobs.end(), [&](Scheduled_Job *sj) { 
            return (sj->id == ended_job_id);
        });
        // @note LH: check if finished job exists
        if (j_iter != _scheduled_jobs.end()){
            auto j_index = distance(_scheduled_jobs.begin(), j_iter);
            _tmp_job = _scheduled_jobs.at(j_index);

            // @note LH: set real finish time
            _tmp_job->real_finish_time = date;

            // @note LH: print csv row to output csv
            auto row_str = batsched_tools::string_format(row_fmt,
                _tmp_job->id.c_str(),
                _tmp_job->start_time,
                _tmp_job->run_time,
                _tmp_job->est_finish_time,
                _tmp_job->real_finish_time
            );
            TCSV_F(b_log::CSV, date, "%s", row_str.c_str());
            // @note LH: merge additions
            _available_machines.insert(_tmp_job->allocated_machines);
            _nb_available_machines += _tmp_job->requested_resources;
            LOG_F(ERROR, "FINISHED Job_ID[%s]: allocated_machines = %s",_tmp_job->id.c_str(), _tmp_job->allocated_machines.to_string_elements().c_str());
            

            // @note LH: remove the finished job 
            _scheduled_jobs.erase(j_iter);

        }
        _tmp_job = NULL; // set tmp job to nothing
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

    // @todo LH: DELETE THIS
    _schedule.update_first_slice(date);

    // Queue sorting
    const Job * priority_job_after = nullptr;
    sort_queue_while_handling_priority_job(priority_job_before, priority_job_after, update_info, compare_info);

    // If no resources have been released, we can just try to backfill the newly-released jobs
    if (_jobs_ended_recently.empty())
    {
        int nb_available_machines = _nb_available_machines;

        _is_priority==false;
        for (unsigned int i = 0; i < recently_queued_jobs.size() && nb_available_machines > 0; ++i)
        {
            const string & new_job_id = recently_queued_jobs[i];
            const Job * new_job = (*_workload)[new_job_id];

            // The job could have already been executed by sort_queue_while_handling_priority_job,
            // that's why we check whether the queue contains the job.
            if (_queue->contains_job(new_job) 
                && new_job != priority_job_after 
                && new_job->nb_requested_resources <= nb_available_machines)
            {
                // @note LH: make_decsions() => if{} => for{} => check_priority_job()
                check_priority_job(new_job, _is_priority, date); 
/*                
                if(_tmp_job->allocated_machines != NULL){
                    _decision->add_execute_job(new_job_id, _tmp_job->allocated_machines, date);
                    _queue->remove_job(new_job);
                    _backfill_counter++;
                }
*/              
                // @todo LH: DELETE THIS
                JobAlloc alloc = _schedule.add_job_first_fit(new_job, _selector);
                if ( alloc.started_in_first_slice)
                {
                    _available_machines -= _allocated_machines;
                    _nb_available_machines -= _tmp_job->requested_resources;

                    LOG_F(ERROR, "job_id[%s]: is excuting....",new_job_id.c_str());
                    _decision->add_execute_job(new_job_id, alloc.used_machines, date);
                    _queue->remove_job(new_job);
                    nb_available_machines -= new_job->nb_requested_resources;
                    _backfill_counter++;
                }
                else
                    _schedule.remove_job(new_job);
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
            
            // @todo LH: remove this
            if (_schedule.contains_job(job))
                _schedule.remove_job(job);

            remove_scheduled_job(priority_job_after->id);   


            
            _is_priority = job == priority_job_after ? true : false;
             // @note LH: [make_decisions()] => [JOB FINISHED = FALSE] => [NEXT JOB PRIORITY = TRUE]
            check_priority_job(job, _is_priority, date);

             // @todo LH: DELETE THIS
            JobAlloc alloc = _schedule.add_job_first_fit(job, _selector);


            if (alloc.started_in_first_slice)
            {
                
                _available_machines -= _allocated_machines;
                _nb_available_machines -= _tmp_job->requested_resources;

                LOG_F(ERROR, "job_id[%s]: is excuting....",job->id.c_str());
                _decision->add_execute_job(job->id, alloc.used_machines, date);
                job_it = _queue->remove_job(job_it); // Updating job_it to remove on traversal
                priority_job_after = _queue->first_job_or_nullptr();
                _backfill_counter++;
                if(_is_priority) priority_job_after = _queue->first_job_or_nullptr();
            }
            else{
                if(!_is_priority) _schedule.remove_job(job);
                ++job_it;
            }
                
/*
            if(_tmp_job->allocated_machines != NULL){
                _decision->add_execute_job(job->id, _tmp_job->allocated_machines, date);
                job_it = _queue->remove_job(job_it); // Updating job_it to remove on traversal
                priority_job_after = _queue->first_job_or_nullptr();
                _backfill_counter++;
            }
*/

        }

    }

    // @note LH: adds queuing info to the out_jobs_extra.csv file
    _decision->add_generic_notification("queue_size",std::to_string(_queue->nb_jobs()),date);
    _decision->add_generic_notification("schedule_size",std::to_string(_scheduled_jobs.size()),date);
    // @todo LH: fix this
    _decision->add_generic_notification("number_running_jobs",std::to_string(_schedule.get_number_of_running_jobs()),date);
    _decision->add_generic_notification("utilization",std::to_string(_schedule.get_utilization()),date);
    _decision->add_generic_notification("utilization_no_resv",std::to_string(_schedule.get_utilization_no_resv()),date);

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
    _is_priority==true;
    // If the priority job has changed
    if (priority_job_after != priority_job_before)
    {
        // @todo LH: DELETE THIS
        if (priority_job_before != nullptr)
            _schedule.remove_job_if_exists(priority_job_before);
        
        if (priority_job_before != nullptr)
            remove_scheduled_job(priority_job_after->id);
        

        

        // Let us ensure the priority job is in the schedule.
        // To do so, while the priority job can be executed now, we keep on inserting it into the schedule
        for (bool could_run_priority_job = true; could_run_priority_job && priority_job_after != nullptr; )
        {
            could_run_priority_job = false;
            // @note LH: (1) Initial scheduling of jobs
            check_priority_job(priority_job_after, _is_priority, update_info->current_date.convert_to<double>());


            // @todo LH: DELETE THIS
            // Let's add the priority job into the schedule
            JobAlloc alloc = _schedule.add_job_first_fit(priority_job_after, _selector);

            if (alloc.started_in_first_slice)
            {

                _available_machines -= _allocated_machines;
                _nb_available_machines -= _tmp_job->requested_resources;
                LOG_F(ERROR, "job_id[%s]: is excuting....",priority_job_after->id.c_str());
                _decision->add_execute_job(priority_job_after->id, alloc.used_machines, update_info->current_date.convert_to<double>());
                _queue->remove_job(priority_job_after);
                priority_job_after = _queue->first_job_or_nullptr();
                could_run_priority_job = true;

            }

/*           
            if(_tmp_job->allocated_machines != NULL){
                _decision->add_execute_job(priority_job_after->id, _tmp_job->allocated_machines, update_info->current_date.convert_to<double>());
                _queue->remove_job(priority_job_after);
                priority_job_after = _queue->first_job_or_nullptr();
                could_run_priority_job = true;
            }
*/ 
        }
    }
}


/* @note LH: added decision function
    - Will the priority job run right now? Yes/No
        - With the nodes currently available
    - If "No", then what is the earliest ext. time the job will run?
    - Keep track of the jobs we "queue" in some list, etc
    - Keep the experiment output to compare later if results are the same
    @todo Need to add a heap sort data structure in the future
    then integrate function into the algorithm
 */
void EasyBackfilling3::check_priority_job(const Job * next_job, bool is_priority, double date)                                                    
{
    _tmp_job = NULL;
    _allocated_machines = NULL;
    // get currently unallocated machines
    // @todo LH: REMOVE THIS
    //machine_count = unused_machines = _schedule.begin()->available_machines.size();
    int machine_count = _nb_available_machines;
    // check if next job can run or not
    _can_run = next_job->nb_requested_resources <=  machine_count;

    // check if the next job is already scheduled 
    auto j_iter = find_if(_scheduled_jobs.begin(), _scheduled_jobs.end(), [&](Scheduled_Job *sj) { 
        return (sj->id == next_job->id);
    });
    _job_exists = j_iter != _scheduled_jobs.end();

    // if next job exists, set a pointer to it, else create a new job object
    if(_job_exists){
        auto j_index = distance(_scheduled_jobs.begin(), j_iter);
        _tmp_job = _scheduled_jobs.at(j_index);
    }else _tmp_job = new Scheduled_Job();

    /* 
        @note checks if the next job can run right now:
        can run: add job to _scheduled_jobs vector or update est. times if job exists
        cannot run: predict the next time it can, update est. times if job exists
    */ 
    if(_can_run){
        _tmp_job->start_time = date;
        _tmp_job->est_finish_time = date + next_job->duration;
        _allocated_machines = _available_machines.left(next_job->nb_requested_resources);
        _tmp_job->allocated_machines = _allocated_machines;
        if(!_job_exists){
            _tmp_job->id = next_job->id;
            _tmp_job->requested_resources = next_job->nb_requested_resources;
            _tmp_job->real_finish_time = 0.0;
            _tmp_job->wall_time = next_job->walltime.convert_to<double>();
            _tmp_job->run_time = next_job->duration;
            _scheduled_jobs.push_back(_tmp_job);
        }
    }
    if(!_can_run && _is_priority){
        for(auto & sj: _scheduled_jobs){
            if(sj->id != next_job->id) continue;
            machine_count += sj->requested_resources;
            if(machine_count >= next_job->nb_requested_resources && _job_exists){
                _tmp_job->start_time = sj->est_finish_time;
                _tmp_job->est_finish_time = _tmp_job->start_time + (_job_exists ? _tmp_job->run_time : next_job->duration);
                break;
            }
        } 
    }
    schedule_heap_sort(_scheduled_jobs.size());
}

void EasyBackfilling3::remove_scheduled_job(string job_id) {

    auto j_iter = find_if(_scheduled_jobs.begin(), _scheduled_jobs.end(), [&](Scheduled_Job *sj) { 
        return (sj->id == job_id);
    });
    if(j_iter != _scheduled_jobs.end()) _scheduled_jobs.erase(j_iter);
}


void EasyBackfilling3::max_heap(int root, int size) {
    // Find largest among root, left child and right child
    int max = root, left = (2 * root) + 1, right = left + 1;
    if (left < size && _scheduled_jobs[left]->est_finish_time > _scheduled_jobs[max]->est_finish_time)
        max = left;
    if (right < size && _scheduled_jobs[right]->est_finish_time  > _scheduled_jobs[max]->est_finish_time)
        max = right;
    // Swap and continue heapifying if root is not largest
    if (max != root) {
        swap(_scheduled_jobs[root], _scheduled_jobs[max]);
        max_heap(max, size);
    }
}


void EasyBackfilling3::schedule_heap_sort(int size)
{
    for(int i = size / 2; i >= 0; i--)
        max_heap(i, size);
    int n = size;
    for(int i = size - 1; i > 0; i--){
        swap(_scheduled_jobs[0], _scheduled_jobs[i]);
        n--;
        max_heap(0, n);
    }
    
    string fmt = "%s,%.15f,%.15f,%.15f,%.15f,%s";
    LOG_F(ERROR, "------------------------------------------------------------------------");
    for(auto & sj: _scheduled_jobs){
        auto res_str = batsched_tools::string_format(fmt,
                sj->id.c_str(),
                sj->start_time,
                sj->run_time,
                sj->est_finish_time,
                sj->real_finish_time,
                sj->allocated_machines.to_string_hyphen().c_str()
        );
        LOG_F(ERROR, "%s", res_str.c_str());
    }
    LOG_F(ERROR, "------------------------------------------------------------------------");
    
}
