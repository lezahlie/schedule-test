#include "easy_bf3.hpp"
#include <loguru.hpp>
#include "../pempek_assert.hpp"
#include "../batsched_tools.hpp"
using namespace std;


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
    _logTime = new b_log();
    _p_job = new Priority_Job();
}

EasyBackfilling3::~EasyBackfilling3()
{
    // @note deallocate priority job struct
    delete _p_job;
}

void EasyBackfilling3::on_simulation_start(double date, const rapidjson::Value & batsim_event)
{
    // @note LH added for time analysis
    GET_TIME(_begin_overall);

    //added
    pid_t pid = batsched_tools::get_batsched_pid();
    _decision->add_generic_notification("PID",std::to_string(pid),date);
    const rapidjson::Value & batsim_config = batsim_event["config"];

    LOG_F(INFO,"on simulation start");
    _output_svg=batsim_config["output-svg"].GetString();
    _svg_frame_start = batsim_config["svg-frame-start"].GetInt64();
    _svg_frame_end = batsim_config["svg-frame-end"].GetInt64();
    _svg_output_start = batsim_config["svg-output-start"].GetInt64();
    _svg_output_end = batsim_config["svg-output-end"].GetInt64();
    LOG_F(INFO,"output svg %s",_output_svg.c_str());

    //was there
    _schedule = Schedule(_nb_machines, date);
    _schedule.set_output_svg(_output_svg);
    _schedule.set_svg_frame_and_output_start_and_end(_svg_frame_start,_svg_frame_end,_svg_output_start,_svg_output_end);
    _schedule.set_svg_prefix(_output_folder + "/svg/");
    _schedule.set_policies(_reschedule_policy,_impact_policy);
    ISchedulingAlgorithm::set_generators(date);
    _recently_under_repair_machines = IntervalSet::empty_interval_set();

    _output_folder=batsim_config["output-folder"].GetString();
    _output_folder.replace(_output_folder.rfind("/out"), std::string("/out").size(), "");
    LOG_F(INFO,"output folder %s",_output_folder.c_str());


    //@note LH: the numbers of jobs in the schedule will be at most the number of machines
    _scheduled_jobs.reserve(_nb_machines);
    //@note LH: added csv file to update timing data (using append mode to record multiple simulations)
    string time_dir="experiments/";
    string time_path= _output_folder.substr(0,_output_folder.find(time_dir));
    _logTime->update_log_file(time_path+time_dir+SRC_FILE+"_time_data.csv",b_log::TIME);
    //@note LH: added to keeop up with machines
    _available_machines.insert(IntervalSet::ClosedInterval(0, _nb_machines - 1));
    _nb_available_machines = _nb_machines;
    PPK_ASSERT_ERROR(_available_machines.size() == (unsigned int) _nb_machines);

    //re-intialize queue if necessary
    if (batsim_config["queue-policy"].GetString() == "ORIGINAL-FCFS")
    {
        //ok we need to delete the _queue pointer and make a new queue
        delete _queue;
        SortableJobOrder * order = new OriginalFCFSOrder;
        _queue = new Queue(order);
    }

    (void) batsim_config;
}

void EasyBackfilling3::on_simulation_end(double date)
{

    // @note LH added for time analysis
    double end_overall = 0.0;
    GET_TIME(end_overall);
    // @note create csv row with simulation timing data
    string row_fmt = "%d,%d,%d,%.15f,%.15f";
    auto time_str = batsched_tools::string_format(
            row_fmt,
                _workload->nb_jobs(),
                _nb_machines,
                _backfill_counter,
                end_overall-_begin_overall,
                _decision_time
    );
    //  @note update csv file with the timing data
    TCSV_F(b_log::TIME, date, "%s", time_str.c_str());
    (void) date;
    
}

void EasyBackfilling3::set_machines(Machines *m){
    _machines = m;
}



void EasyBackfilling3::on_requested_call(double date,int id,batsched_tools::call_me_later_types forWhat)
{
    if (_output_svg != "none")
        _schedule.set_now((Rational)date);
    switch (forWhat){
        
        case batsched_tools::call_me_later_types::SMTBF:
            {
                //Log the failure
                //BLOG_F(b_log::FAILURES,"FAILURE SMTBF");
                if (_schedule.get_number_of_running_jobs() > 0 || !_queue->is_empty() || !_no_more_static_job_to_submit_received)
                    {
                        double number = failure_exponential_distribution->operator()(generator_failure);
                        LOG_F(INFO,"%f %f",_workload->_repair_time,_workload->_MTTR);
                        if (_workload->_repair_time == 0.0 && _workload->_MTTR == -1.0)
                            _on_machine_instant_down_ups.push_back(batsched_tools::KILL_TYPES::SMTBF);                                        
                        else
                            _on_machine_down_for_repairs.push_back(batsched_tools::KILL_TYPES::SMTBF);
                        _decision->add_call_me_later(batsched_tools::call_me_later_types::SMTBF,1,number+date,date);
                    }
            }
            break;
        /* TODO
        case batsched_tools::call_me_later_types::MTBF:
                    {
                        if (!_running_jobs.empty() || !_pending_jobs.empty() || !_no_more_static_job_to_submit_received)
                        {
                            double number = distribution->operator()(generator);
                            on_myKillJob_notify_event(date);
                            _decision->add_call_me_later(batsched_tools::call_me_later_types::MTBF,1,number+date,date);

                        }
                    
                        
                    }
                    break;
        */
        case batsched_tools::call_me_later_types::FIXED_FAILURE:
            {
                //BLOG_F(b_log::FAILURES,"FAILURE FIXED_FAILURE");
                LOG_F(INFO,"DEBUG");
                if (_schedule.get_number_of_running_jobs() > 0 || !_queue->is_empty() || !_no_more_static_job_to_submit_received)
                    {
                        LOG_F(INFO,"DEBUG");
                        double number = _workload->_fixed_failures;
                        if (_workload->_repair_time == 0.0 & _workload->_MTTR == -1.0)
                            _on_machine_instant_down_ups.push_back(batsched_tools::KILL_TYPES::FIXED_FAILURE);//defer to after make_decisions
                        else
                            _on_machine_down_for_repairs.push_back(batsched_tools::KILL_TYPES::FIXED_FAILURE);
                        _decision->add_call_me_later(batsched_tools::call_me_later_types::FIXED_FAILURE,1,number+date,date);
                    }
            }
            break;
        
        case batsched_tools::call_me_later_types::REPAIR_DONE:
            {
                //BLOG_F(b_log::FAILURES,"REPAIR_DONE");
                //a repair is done, all that needs to happen is add the machines to available
                //and remove them from repair machines and add one to the number of available
                if (_output_svg == "all")
                    _schedule.output_to_svg("top Repair Done  Machine #: "+std::to_string(id));
                IntervalSet machine = id;
                _schedule.remove_repair_machines(machine);
                _schedule.remove_svg_highlight_machines(machine);
                if (_output_svg == "all")
                    _schedule.output_to_svg("bottom Repair Done  Machine #: "+std::to_string(id));

                //LOG_F(INFO,"in repair_machines.size(): %d nb_avail: %d avail: %d  running_jobs: %d",_repair_machines.size(),_nb_available_machines,_available_machines.size(),_running_jobs.size());
            }
            break;
        
        case batsched_tools::call_me_later_types::RESERVATION_START:
            {
                _start_a_reservation = true;
                //SortableJobOrder::UpdateInformation update_info(date);
                //make_decisions(date,&update_info,nullptr);
                
            }
            break;
    }
    
}


void EasyBackfilling3::on_machine_down_for_repair(batsched_tools::KILL_TYPES forWhat,double date){
    (void) date;
    auto sort_original_submit_pair = [](const std::pair<const Job *,IntervalSet> j1,const std::pair<const Job *,IntervalSet> j2)->bool{
            if (j1.first->submission_times[0] == j2.first->submission_times[0])
                return j1.first->id < j2.first->id;
            else
                return j1.first->submission_times[0] < j2.first->submission_times[0];
    };

    //get a random number of a machine to kill
    int number = machine_unif_distribution->operator()(generator_machine);
    //make it an intervalset so we can find the intersection of it with current allocations
    IntervalSet machine = (*_machines)[number]->id;
    
    if (_output_svg == "all")
            _schedule.output_to_svg("On Machine Down For Repairs  Machine #:  "+std::to_string((*_machines)[number]->id));
    double repair_time = (*_machines)[number]->repair_time;
    //if there is a global repair time set that as the repair time
    if (_workload->_repair_time != -1.0)
        repair_time = _workload->_repair_time;
    if (_workload->_MTTR != -1.0)
        repair_time = repair_time_exponential_distribution->operator()(generator_repair_time);
    IntervalSet added = IntervalSet::empty_interval_set() ;
    if (_schedule.get_reservations_running_on_machines(machine).empty())
        added = _schedule.add_repair_machine(machine,repair_time);

    LOG_F(INFO,"here");
    //if the machine is already down for repairs ignore it.
    //LOG_F(INFO,"repair_machines.size(): %d    nb_avail: %d  avail:%d running_jobs: %d",_repair_machines.size(),_nb_available_machines,_available_machines.size(),_running_jobs.size());
    //BLOG_F(b_log::FAILURES,"Machine Repair: %d",number);
    if (!added.is_empty())
    {
        _recently_under_repair_machines+=machine; //haven't found a use for this yet
        _schedule.add_svg_highlight_machines(machine);
        //ok the machine is not down for repairs already so it WAS added
        //the failure/repair will not be happening on a machine that has a reservation on it either
        //it will be going down for repairs now
        
        //call me back when the repair is done
        _decision->add_call_me_later(batsched_tools::call_me_later_types::REPAIR_DONE,number,date+repair_time,date);

        if (_schedule.get_number_of_running_jobs() > 0 )
        {
            //there are possibly some running jobs to kill

            std::vector<std::string> jobs_to_kill;
            _schedule.get_jobs_running_on_machines(machine,jobs_to_kill);

            std::string jobs_to_kill_str = !(jobs_to_kill.empty())? std::accumulate( /* otherwise, accumulate */
            ++jobs_to_kill.begin(), jobs_to_kill.end(), /* the range 2nd to after-last */
            *jobs_to_kill.begin(), /* and start accumulating with the first item */
            [](auto& a, auto& b) { return a + "," + b; }) : "";

            LOG_F(INFO,"jobs to kill %s",jobs_to_kill_str.c_str());

            if (!jobs_to_kill.empty()){
                
                std::vector<batsched_tools::Job_Message *> msgs;
                for (auto job_id : jobs_to_kill){
                    auto msg = new batsched_tools::Job_Message;
                    msg->id = job_id;
                    msg->forWhat = forWhat;
                    msgs.push_back(msg);
                }
                _killed_jobs=true;
                _decision->add_kill_job(msgs,date);
                for (auto job_id:jobs_to_kill)
                    _schedule.remove_job_if_exists((*_workload)[job_id]);
            }
            //in conservative_bf we reschedule everything
            //in easy_bf only backfilled jobs,running jobs and priority job is scheduled
            //but there may not be enough machines to run the priority job
            //move the priority job to after the repair time and let things backfill ahead of that.
            if (_output_svg == "all")
                _schedule.output_to_svg("Finished Machine Down For Repairs, Machine #: "+std::to_string(number));
        }
    }
    else{
        //if (!added.is_empty())
        //  _schedule.remove_repair_machines(machine);
        //_schedule.remove_svg_highlight_machines(machine);
        if (_output_svg == "all")
            _schedule.output_to_svg("Finished Machine Down For Repairs, NO REPAIR  Machine #:  "+std::to_string(number));
    
    }
}

void EasyBackfilling3::on_machine_instant_down_up(batsched_tools::KILL_TYPES forWhat,double date){
    (void) date;
    //get a random number of a machine to kill
    int number = machine_unif_distribution->operator()(generator_machine);
    //make it an intervalset so we can find the intersection of it with current allocations
    IntervalSet machine = number;
    _schedule.add_svg_highlight_machines(machine);
    if (_output_svg == "all")
            _schedule.output_to_svg("On Machine Instant Down Up  Machine #: "+std::to_string(number));
    
    //BLOG_F(b_log::FAILURES,"Machine Instant Down Up: %d",number);
    LOG_F(INFO,"instant down up machine number %d",number);
    //if there are no running jobs, then there are none to kill
    if (_schedule.get_number_of_running_jobs() > 0){
        //ok so there are running jobs
        LOG_F(INFO,"instant down up");
        std::vector<std::string> jobs_to_kill;
        _schedule.get_jobs_running_on_machines(machine,jobs_to_kill);
         LOG_F(INFO,"instant down up");
        
        LOG_F(INFO,"instant down up");
        if (!jobs_to_kill.empty())
        {
            _killed_jobs = true;
            std::vector<batsched_tools::Job_Message *> msgs;
            for (auto job_id : jobs_to_kill){
                auto msg = new batsched_tools::Job_Message;
                msg->id = job_id;
                msg->forWhat = forWhat;
                msgs.push_back(msg);
            }
            _decision->add_kill_job(msgs,date);
            std::string jobs_to_kill_string;
            //remove jobs to kill from schedule and add to our log string
             LOG_F(INFO,"instant down up");
            for (auto job_id:jobs_to_kill)
            {
                jobs_to_kill_string += ", " + job_id;
                _schedule.remove_job_if_exists((*_workload)[job_id]);
            }
             LOG_F(INFO,"instant down up");
            //BLOG_F(b_log::FAILURES,"Killing Jobs: %s",jobs_to_kill_string.c_str());
        }
            	
	}
    _schedule.remove_svg_highlight_machines(machine);
    if (_output_svg == "all")
            _schedule.output_to_svg("END On Machine Instant Down Up  Machine #: "+std::to_string(number));
}


// @note func: make_decisions()
void EasyBackfilling3::make_decisions(double date,
                                     SortableJobOrder::UpdateInformation *update_info,
                                     SortableJobOrder::CompareInformation *compare_info)
{
    // @note LH added for time analysis
    GET_TIME(_begin_decision);

    const Job * priority_job_before = _queue->first_job_or_nullptr();

    // Let's remove finished jobs from the schedule
    for (const string & ended_job_id : _jobs_ended_recently){
        handle_finished_job(ended_job_id, date);
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
        for (unsigned int i = 0; i < recently_queued_jobs.size() && _nb_available_machines > 0; ++i)
        {
            const string & new_job_id = recently_queued_jobs[i];
            const Job * new_job = (*_workload)[new_job_id];
            // The job could have already been executed by sort_queue_while_handling_priority_job,
            // that's why we check whether the queue contains the job.
            if (_queue->contains_job(new_job) && 
                new_job != priority_job_after)
            {
                check_next_job(new_job, date);
                if(_can_run){
                    _decision->add_execute_job(new_job_id, _tmp_job->allocated_machines, date);
                    _queue->remove_job(new_job);
                }
            }
        }
    }
    else
    {        // Some resources have been released, the whole queue should be traversed.
        auto job_it = _queue->begin();
        // Let's try to backfill all the jobs
        while (job_it != _queue->end() && _nb_available_machines > 0)
        {
            const Job * job = (*job_it)->job;

            if(job == priority_job_after) check_priority_job(job, date);
            else check_next_job(job, date);
            
            if(_can_run){
                _decision->add_execute_job(job->id, _tmp_job->allocated_machines, date);
                job_it = _queue->remove_job(job_it); // Updating job_it to remove on traversal
                if(job == priority_job_after) priority_job_after = _queue->first_job_or_nullptr();
            }else ++job_it;  
        }
    }
    

    // @note LH: adds queuing info to the out_jobs_extra.csv file
    _decision->add_generic_notification("queue_size",std::to_string(_queue->nb_jobs()),date);
    _decision->add_generic_notification("schedule_size",std::to_string(_scheduled_jobs.size()),date);
/*    // @todo LH: fix this
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
        // Let us ensure the priority job is in the schedule.
        // To do so, while the priority job can be executed now, we keep on inserting it into the schedule
        for (bool could_run_priority_job = true; could_run_priority_job && priority_job_after != nullptr; )
        {
            could_run_priority_job = false;

            // @note LH: (1) Initial scheduling of jobs
            check_priority_job(priority_job_after, C2DBL(update_info->current_date));
            if(_can_run){
                _decision->add_execute_job(priority_job_after->id, _tmp_job->allocated_machines, C2DBL(update_info->current_date));
                _queue->remove_job(priority_job_after);
                priority_job_after = _queue->first_job_or_nullptr();
                could_run_priority_job = true;
            }
        }
    }
}

//@note LH: added function check priority job and "reserve" it's spot in the schedule
void EasyBackfilling3::check_priority_job(const Job * priority_job, double date){   

    int machine_count = _nb_available_machines;

    // @note update priority job if it changed
    if(_p_job->id != priority_job->id){
        _p_job->id = priority_job->id;
        _p_job->requested_resources = priority_job->nb_requested_resources;
    }

    /* @note 
        priority job can run if the following is true:
            - requested resources <=  current available 
    */
    _can_run = _p_job->requested_resources <= machine_count;

    if(_can_run){

        // @note priority job can run so add it to the schedule
        handle_scheduled_job(priority_job,date);

        // @note sort the schedule
        sort_max_heap(_scheduled_jobs.size());
    }else{

        // @note if the priority job can't run then calculate when it will 
        for(auto & sj: _scheduled_jobs){
            machine_count += sj->requested_resources;
            if(machine_count >= _p_job->requested_resources){
                // @note predicted start time
                _p_job->shadow_time = sj->est_finish_time;
                // @note predicted end time
                _p_job->est_finish_time = sj->est_finish_time + C2DBL(priority_job->walltime);
                // @note available resources after priority jobs reserved start time
                _p_job->extra_resources = machine_count - priority_job->nb_requested_resources;
                break;
            }
        }
    }
}

//@note LH: added function check if next job can be backfilled
void EasyBackfilling3::check_next_job(const Job * next_job, double date){   

    /* @note LH:
        job can be backfilled if the following is true:
            - job will finish before the priority jobs reserved start (shadow) time 
                -AND- the requested resources are <= current available resources
            - otherwise job finishs after priority jobs start time 
                -AND- the requested resources are <= MIN[current available nodes, waiting priority job extra nodes]
    */
    _can_run = ((date+C2DBL(next_job->walltime)) <= _p_job->shadow_time)
        ? (next_job->nb_requested_resources <= _nb_available_machines) 
        : (next_job->nb_requested_resources <= MIN(_nb_available_machines ,_p_job->extra_resources));

    /* @note LH:
        job can be backfilled, 
            - add it to the schedule
            - sort the schedule
            - increase backfilled jobs count
    */ 
    if(_can_run){
        handle_scheduled_job(next_job, date);
        sort_max_heap(_scheduled_jobs.size());
        _backfill_counter++;
    }
}

//@note LH: added function to add jobs to the schedule
void EasyBackfilling3::handle_scheduled_job(const Job * job, double date){
    // allocate space for scheduled job
    _tmp_job = new Scheduled_Job();
    // @note convert wall time to a double
    double tmp_walltime = C2DBL(job->walltime);

    // @note set the scheduled jobs info
    _tmp_job->id = job->id;
    _tmp_job->requested_resources = job->nb_requested_resources;
    _tmp_job->wall_time = tmp_walltime;
    _tmp_job->run_time = job->duration;
    _tmp_job->start_time = date;
    _tmp_job->est_finish_time = date + tmp_walltime;
    _tmp_job->allocated_machines = _available_machines.left(job->nb_requested_resources);
    // @note add the job to the schedule
    _scheduled_jobs.push_back(_tmp_job);

    // @note remove allocated nodes from intervalset and subtract from machine count
    _available_machines -= _tmp_job->allocated_machines;
    _nb_available_machines -= _tmp_job->requested_resources;
}

void EasyBackfilling3::handle_finished_job(string job_id, double date){
    // @note LH: get finished job from scheduler
    auto j_iter = std::find_if(_scheduled_jobs.begin(), _scheduled_jobs.end(), [&](Scheduled_Job *sj) { 
        return (sj->id == job_id);
    });
    if(j_iter != _scheduled_jobs.end()){
        auto j_index = distance(_scheduled_jobs.begin(), j_iter);
        _tmp_job = _scheduled_jobs.at(j_index);
        // @note LH: return allocated machines to intervalset and add to machine count
        _available_machines.insert(_tmp_job->allocated_machines);
        _nb_available_machines += _tmp_job->requested_resources;

        // @note deallocate finished job struct
        delete _tmp_job;
        // @note LH: delete the pointer to the job struct from the vector
        _scheduled_jobs.erase(j_iter);
    }
}

//@note LH: added helper sub-function to turn schedule into a maximum heap
void EasyBackfilling3::max_heapify(int root, int size){
    // @note find the max root node, left node, and right node
    int max = root, left = (2 * root) + 1, right = left + 1;

    // @note LH: if not the last node -AND- the left is more than the root, set left node as new max root
    if (left < size && _scheduled_jobs[left]->est_finish_time > _scheduled_jobs[max]->est_finish_time){
        max = left;
    }
    // @note LH: if not the last node -AND- right node is more than the root, set right node as new max root
    if (right < size && _scheduled_jobs[right]->est_finish_time  > _scheduled_jobs[max]->est_finish_time){
        max = right;
    }

    // @note swap[max_root(max),last_node(size)] and heapify the new max root
    if (max != root) {
        swap(_scheduled_jobs[root], _scheduled_jobs[max]);
        max_heapify(max, size);
    }
}

//@note LH: added heaper function for heap sorting the schedule
void EasyBackfilling3::sort_max_heap(int size){
    //@note LH: no need to sort if there is less than 2 jobs
    if(size < 2) return;

    // @note LH: build the new max heap
    for(int i = size / 2; i >= 0; i--){
        max_heapify(i, size);
    }

    // @note LH: sort the new max heap
    for(int j = size - 1; j > 0; j--){
        swap(_scheduled_jobs[0], _scheduled_jobs[j]);
        max_heapify(0, j);
    }
}
