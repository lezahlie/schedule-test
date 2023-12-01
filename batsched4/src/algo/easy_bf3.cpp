#include "easy_bf3.hpp"

#include <loguru.hpp>

#include "../pempek_assert.hpp"

//added
#include "../batsched_tools.hpp"

using namespace std;


// @note LH: testing macros
#define T_LOG_INSTANCE _testBLOG
#define SRC_FILE "easy_bf3.cpp"



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
    // @note LH: test log file
    _testBLOG = new b_log();
}

EasyBackfilling3::~EasyBackfilling3()
{

}

void EasyBackfilling3::on_simulation_start(double date, const rapidjson::Value & batsim_event)
{
   //added
    pid_t pid = batsched_tools::get_batsched_pid();
    _decision->add_generic_notification("PID",std::to_string(pid),date);
    const rapidjson::Value & batsim_config = batsim_event["config"];
    LOG_F(INFO,"ON simulation start");
    _output_svg=batsim_config["output-svg"].GetString();
    _svg_frame_start = batsim_config["svg-frame-start"].GetInt64();
    _svg_frame_end = batsim_config["svg-frame-end"].GetInt64();
    _svg_output_start = batsim_config["svg-output-start"].GetInt64();
    _svg_output_end = batsim_config["svg-output-end"].GetInt64();
    LOG_F(INFO,"output svg %s",_output_svg.c_str());
    
    _output_folder=batsim_config["output-folder"].GetString();
    
    _output_folder.replace(_output_folder.rfind("/out"), std::string("/out").size(), "");
    
    LOG_F(INFO,"output folder %s",_output_folder.c_str());
    
    // @note LH: create log file for testing
    _testBLOG->add_log_file(_output_folder+"/log/test_jobs.log",b_log::TEST);
    TLOG_F(b_log::TEST, date, SRC_FILE, "Test log for easybf_3");


    Schedule::convert_policy(batsim_config["reschedule-policy"].GetString(),_reschedule_policy);
    Schedule::convert_policy(batsim_config["impact-policy"].GetString(),_impact_policy);
    
    
    //was there
    _schedule = Schedule(_nb_machines, date);
    //added
    _schedule.set_output_svg(_output_svg);
    _schedule.set_svg_frame_and_output_start_and_end(_svg_frame_start,_svg_frame_end,_svg_output_start,_svg_output_end);
    _schedule.set_svg_prefix(_output_folder + "/svg/");
    _schedule.set_policies(_reschedule_policy,_impact_policy);
    ISchedulingAlgorithm::set_generators(date);
    
    _recently_under_repair_machines = IntervalSet::empty_interval_set();

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
    analyze_endtime_diffs(date);
    (void) date;
    
}

void EasyBackfilling3::set_machines(Machines *m){
    _machines = m;
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
    //LOG_F(INFO,"repair_machines.size(): %d    nb_avail: %d  avail:%d _scheduled_jobs: %d",_repair_machines.size(),_nb_available_machines,_available_machines.size(),_running_jobs.size());
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
                           
                           //LOG_F(INFO,"in repair_machines.size(): %d nb_avail: %d avail: %d  _scheduled_jobs: %d",_repair_machines.size(),_nb_available_machines,_available_machines.size(),_running_jobs.size());
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

// @note func: make_decisions()
void EasyBackfilling3::make_decisions(double date,
                                     SortableJobOrder::UpdateInformation *update_info,
                                     SortableJobOrder::CompareInformation *compare_info)
{
    const Job * priority_job_before = _queue->first_job_or_nullptr();
    string fmt = "FINISHED_JOB_ID = [%s]: RUN_TIME = [%.15f], START TIME = [%.15f], EST FINISH TIME: [%.15f], REAL FINISH TIME: [%.15f] || FINISH_TIME_DIFF = [%.15f], DIFFERENCE_COUNT = [%d]\n";
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

            // @note check for floating point differences
            _finish_time_diff = fabs(_tmp_job->real_finish_time - _tmp_job->est_finish_time);

            // @note record the difference in a temporary map 
            _finish_time_diff_map.try_emplace(_tmp_job->id, _finish_time_diff);


            // @note get the floating point differences between est and real finish times
            if(_finish_time_diff == 0.0){
                _decision_exact = true;
            }else{
                _decision_exact = false;
                _exact_diff_count+=1;      
            }

            // @note check for floating point differences less than epsilon for doubles
            auto res_str = batsched_tools::string_format(fmt,
                _tmp_job->id.c_str(), 
                _tmp_job->run_time,
                _tmp_job->start_time,
                _tmp_job->est_finish_time,
                _tmp_job->real_finish_time,
                _finish_time_diff,
                _exact_diff_count
            );

            //TLOG_F(b_log::TEST,  update_info->current_date.convert_to<double>(), SRC_FILE, "%s", res_str.c_str());

            // @note LH: remove the finished job 
            _scheduled_jobs.erase(j_iter);
        }

        // @note calculating differences and logging
        _tmp_job = NULL; // set sj to nothing
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

    // Let's update the schedule's present
    _schedule.update_first_slice(date);

    //We will want to handle any Failures before we start allowing anything new to run
    //This is very important for when there are repair times, as the machine may be down

    //handle any instant down ups (no repair time on machine going down)
    for(batsched_tools::KILL_TYPES forWhat : _on_machine_instant_down_ups)
    {
        on_machine_instant_down_up(forWhat,date);
    }
    //ok we handled them all, clear the container
    _on_machine_instant_down_ups.clear();
    //handle any machine down for repairs (machine going down with a repair time)
    for(batsched_tools::KILL_TYPES forWhat : _on_machine_down_for_repairs)
    {
        on_machine_down_for_repair(forWhat,date);
    }
    //ok we handled them all, clear the container
    _on_machine_down_for_repairs.clear();

    // Queue sorting
    const Job * priority_job_after = nullptr;
    sort_queue_while_handling_priority_job(priority_job_before, priority_job_after, update_info, compare_info);

    // If no resources have been released, we can just try to backfill the newly-released jobs
    if (_jobs_ended_recently.empty())
    {
        int nb_available_machines = _schedule.begin()->available_machines.size();

        for (unsigned int i = 0; i < recently_queued_jobs.size() && nb_available_machines > 0; ++i)
        {
            const string & new_job_id = recently_queued_jobs[i];
            const Job * new_job = (*_workload)[new_job_id];

            // The job could have already been executed by sort_queue_while_handling_priority_job,
            // that's why we check whether the queue contains the job.
            if (_queue->contains_job(new_job) &&
                new_job != priority_job_after &&
                new_job->nb_requested_resources <= nb_available_machines)
            {
                // @note LH: make_decsions() => if{} => for{} => check_priority_job()
                // TLOG_F(b_log::TEST, update_info->current_date.convert_to<double>(), SRC_FILE, "LINE: 400 || LOCATION: [make_decisions() => {IF-FOR-IF} => add_job_first_fit()] - (A) || JOB_ID = %s", new_job->id.c_str());
                check_priority_job(new_job, date); 

                JobAlloc alloc = _schedule.add_job_first_fit(new_job, _selector);
                if ( alloc.started_in_first_slice)
                {
                    _decision->add_execute_job(new_job_id, alloc.used_machines, date);
                    _queue->remove_job(new_job);
                    nb_available_machines -= new_job->nb_requested_resources;
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
        int nb_available_machines = _schedule.begin()->available_machines.size();

        // Let's try to backfill all the jobs
        while (job_it != _queue->end() && nb_available_machines > 0)
        {
            const Job * job = (*job_it)->job;

            if (_schedule.contains_job(job))
                _schedule.remove_job(job);

            if (job == priority_job_after) // If the current job is priority
            {

                // @note LH: [make_decisions()] => [JOB FINISHED = FALSE] => [NEXT JOB PRIORITY = TRUE]
                //TLOG_F(b_log::TEST, update_info->current_date.convert_to<double>(), SRC_FILE, "LINE: 440 || LOCATION: [make_decisions() => {ELSE-WHILE-IF} =>  add_job_first_fit()] - (B) || ADD JOB_ID = %s", job->id.c_str());
                check_priority_job(job, date);
                JobAlloc alloc = _schedule.add_job_first_fit(job, _selector);

                if (alloc.started_in_first_slice)
                {
                    _decision->add_execute_job(job->id, alloc.used_machines, date);
                    job_it = _queue->remove_job(job_it); // Updating job_it to remove on traversal
                    priority_job_after = _queue->first_job_or_nullptr();
                }
                else
                    ++job_it;
            }
            else // The job is not priority
            {
                // @note LH: [make_decisions()] => [else{} => while{} => else{}] => check_priority_job()
                //TLOG_F(b_log::TEST, update_info->current_date.convert_to<double>(), SRC_FILE, "LINE: 459 || LOCATION: [make_decisions() => {ELSE-WHILE-ELSE} => add_job_first_fit()] - (C) || ADD JOB_ID = %s", job->id.c_str());
                check_priority_job(job, date);
                JobAlloc alloc = _schedule.add_job_first_fit(job, _selector);

                if (alloc.started_in_first_slice)
                {
                    _decision->add_execute_job(job->id, alloc.used_machines, date);
                    job_it = _queue->remove_job(job_it);
                }
                else
                {
                    _schedule.remove_job(job);
                    ++job_it;
                }
            }
        }
    }
}


void EasyBackfilling3::sort_queue_while_handling_priority_job(const Job * priority_job_before,
                                                             const Job *& priority_job_after,
                                                             SortableJobOrder::UpdateInformation * update_info,
                                                             SortableJobOrder::CompareInformation * compare_info)
{
    if (_debug)
        LOG_F(1, "sort_queue_while_handling_priority_job beginning, %s", _schedule.to_string().c_str());

    // Let's sort the queue
    _queue->sort_queue(update_info, compare_info);

    // Let the new priority job be computed
    priority_job_after = _queue->first_job_or_nullptr();

    // If the priority job has changed
    if (priority_job_after != priority_job_before)
    {
        // If there was a priority job before, let it be removed from the schedule
        if (priority_job_before != nullptr)
            _schedule.remove_job_if_exists(priority_job_before);

        // Let us ensure the priority job is in the schedule.
        // To do so, while the priority job can be executed now, we keep on inserting it into the schedule
        for (bool could_run_priority_job = true; could_run_priority_job && priority_job_after != nullptr; )
        {
            could_run_priority_job = false;

            // @note LH: [sort_queue_while_handling_priority_job()] => [if{} => for{}] => check_priority_job()
            //TLOG_F(b_log::TEST, (double)update_info->current_date, SRC_FILE, "LINE: 511 || LOCATION: [sort_queue_while_handling_priority_job() => {IF-FOR} => add_job_first_fit() || ADD JOB_ID = %s", priority_job_after->id.c_str());
            check_priority_job(priority_job_after, update_info->current_date.convert_to<double>());

            // Let's add the priority job into the schedule
            JobAlloc alloc = _schedule.add_job_first_fit(priority_job_after, _selector);

            if (alloc.started_in_first_slice)
            {
                _decision->add_execute_job(priority_job_after->id, alloc.used_machines, update_info->current_date.convert_to<double>());
                _queue->remove_job(priority_job_after);
                priority_job_after = _queue->first_job_or_nullptr();
                could_run_priority_job = true;
            }
        }
    }

    if (_debug)
        LOG_F(1, "sort_queue_while_handling_priority_job ending, %s", _schedule.to_string().c_str());
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
void EasyBackfilling3::check_priority_job(const Job * next_job, double date)                                                    
{
    int unused_nodes = 0, node_count = 0;
    double est_start_time = 0.0, est_finish_time = 0.0;
    bool can_run = false, job_exists = false;
    _tmp_job = NULL;
    // get currently unallocated nodes
    node_count = unused_nodes = _schedule.begin()->available_machines.size();
    // check fi next job can run or not
    can_run = next_job->nb_requested_resources <= unused_nodes;
    // cast walltime to a double
    // sort through the currently running/scheduled jobs
    sort(_scheduled_jobs.begin(), _scheduled_jobs.end(), [&](Scheduled_Job * sj_a, Scheduled_Job * sj_b){
        return sj_a->est_finish_time < sj_b->est_finish_time; 
    });

    // check if the next job is already scheduled 
    auto j_iter = find_if(_scheduled_jobs.begin(), _scheduled_jobs.end(), [&](Scheduled_Job *sj) { 
        return (sj->id == next_job->id);
    });
    job_exists = j_iter != _scheduled_jobs.end() ? true : false;

    // if next job exists, set a pointer to it, else create a new job object
    if(job_exists){
        auto j_index = distance(_scheduled_jobs.begin(), j_iter);
        _tmp_job = _scheduled_jobs.at(j_index);
    }else _tmp_job = new Scheduled_Job();

    /*
        checks if the next job can run right now:
            can run: add job to _scheduled_jobs vector or update est. times if job exists
            cannot run: predict the next time it can, update est. times if job exists
    */ 
    if(can_run){
        _tmp_job->start_time = date;
        _tmp_job->est_finish_time = date + next_job->duration;
        if(!job_exists){
            _tmp_job->id = next_job->id;
            _tmp_job->requested_resources = next_job->nb_requested_resources;
            _tmp_job->real_finish_time = 0.0;
            _tmp_job->wall_time = next_job->walltime.convert_to<double>();
            _tmp_job->run_time = next_job->duration;
            _scheduled_jobs.push_back(_tmp_job);
        }
    }else{
        for(auto & sj: _scheduled_jobs){
            if(sj->id != next_job->id) continue;
            node_count += sj->requested_resources;
            if(node_count >= next_job->nb_requested_resources){
                est_start_time = sj->est_finish_time;
                est_finish_time = est_start_time + (job_exists ? _tmp_job->run_time : next_job->duration);
                if(job_exists){
                    _tmp_job->start_time = est_start_time;
                    _tmp_job->est_finish_time = est_finish_time;
                }
                break;
            }
        } 
    }

    /* 
    // log output for debugging purposes 
    string fmt1 = "", fmt2 = "";
    fmt1 = "JOB_ID = [%s]:  WALL_TIME = [%.15f], REQUESTED_NODES = [%d], AVAILABLE_NODES = [%d], RUNNING_JOBS_NOW = [%d]\nJOB_ID = [%s]: CAN JOB RUN? = [%s]";
    fmt2 = ", EST_START_TIME = [%.15f], EST_FINISH_TIME = [%.15f]";
    auto res_str = batsched_tools::string_format(can_run ? fmt1+'\n' : fmt1+fmt2+'\n',
        next_job->id.c_str(), 
        next_job_walltime,
        next_job->nb_requested_resources,
        can_run ? unused_machines : machine_count,
        _scheduled_jobs.size(),
        next_job->id.c_str(), 
        can_run ? "TRUE" : "FALSE", 
        job_exists ? _tmp_job->start_time : est_start_time, 
        job_exists ? _tmp_job->est_finish_time : est_finish_time
    );
    TLOG_F(b_log::TEST, date, SRC_FILE, "%s", res_str);
    */
}

void EasyBackfilling3::analyze_endtime_diffs(double date){

    // variables to calculate min, max, sum, avgs, and std for all jobs
    double overall_min = 0.0, overall_max = 0.0, overall_sum = 0.0, overall_job_variance = 0.0, overall_diff_variance= 0.0, overall_job_avg = 0.0, overall_diff_avg = 0.0;
    // variables to calculate min, max, sum, avgs, and std for ranges
    double range_min = 0.0, range_max = 0.0, range_sum = 0.0, range_job_variance = 0.0, range_diff_variance = 0.0, range_job_avg = 0.0, range_diff_avg = 0.0;
    // temporary variables to compute the std
    double job_std_final = 0.0, diff_std_final = 0.0;
    // counters and temp range varaible
    int job_counter = 1, range_counter = 0, range_diff_count = 0, range = _workload->nb_jobs()/10;

    // finds the sum of differences for all jobs and ranges of jobs
    for_each(_finish_time_diff_map.begin(),_finish_time_diff_map.end(),[&](const auto &ftd) {

        // check if the job had difference in end time
        if(ftd.second > 0){
            // sum the differences
            overall_sum += ftd.second;
            range_sum += ftd.second;
            range_diff_count+=1;
        }

        // check if the current range has been iterated
        if(job_counter % range == 0){

            // calculate the avg diffs for both all jobs and diffs only
            range_job_avg = range_sum/range;
            range_diff_avg = range_sum/range_diff_count;

            // record avgs and diff counts in temp vectors
            _avg_all_jobs_by_range.push_back(range_job_avg);
            _avg_only_diffs_by_range.push_back(range_diff_avg);
            _diff_count_by_range.push_back(range_diff_count);

            range_sum = 0.0; // reset sum of ranged differences
            range_diff_count = 0;
        }

        // increment the loop counter
        job_counter += 1;
    });

    // calculate the overall avg diffs for both all jobs and diffs only 
    overall_job_avg = overall_sum/_workload->nb_jobs();
    overall_diff_avg = overall_sum/_exact_diff_count;
    // reset loop count to reuse
    job_counter = 1;

    for_each(_finish_time_diff_map.begin(),_finish_time_diff_map.end(),[&](const auto &ftd) {

        // check if the job had difference in end time
        if(ftd.second > 0){

            // finds min difference for the current range
            if(range_min == 0 || ftd.second < range_min) range_min = ftd.second;
            
            // sum the overall std for both all jobs and diffs only
            overall_job_variance += pow(ftd.second - overall_job_avg, 2);
            overall_diff_variance += pow(ftd.second - overall_diff_avg, 2);

            // sum the std of the current range for both all jobs and diffs only
            range_job_variance += pow(ftd.second - _avg_all_jobs_by_range[range_counter], 2);
            range_diff_variance += pow(ftd.second - _avg_only_diffs_by_range[range_counter], 2);
            
            // finds max difference for current range
            if(ftd.second > range_max) range_max = ftd.second;
        }

        // check if the current range has been iterated
        if(job_counter % range == 0){

            // finds the overall min and max differences with the ranged min and max (less work)
            if(overall_min == 0 || range_min < overall_min) overall_min = range_min;
            if(range_max > overall_max) overall_max = range_max;
            // calculate the actual diff std of the current range for both all jobs and diffs only
            job_std_final = sqrt(range_job_variance/(range-1));
            diff_std_final = sqrt(range_diff_variance/(_diff_count_by_range[range_counter]-1));

            TLOG_F(b_log::TEST,
                date, 
                SRC_FILE, 
                "[SECTIONED END_TIME DIFFS]: JOB range = (%d, %d), TOTAL DIFFS = %d, MINIMUM = %.15f, MAXIMUM = %.15f || [ALL JOBS]: AVG = %.15f, STD = %.15f || [DIFFS ONLY]:  AVG  = %.15f, STD = %.15f",
                (job_counter-range), job_counter, _diff_count_by_range[range_counter], range_min, range_max, range_job_avg, job_std_final, range_diff_avg, diff_std_final
            );

            // reset all variables calculated for the next range
            range_diff_variance = range_job_variance = range_min = range_max = range_sum = 0.0;
            // increment the loop ranges counter
            range_counter += 1; 
        }

        // increment the loop counter
        job_counter += 1;
    });

    // calculate the actual diff std of all jobs for both all jobs and diffs only
    job_std_final = sqrt(overall_job_avg/(_workload->nb_jobs()-1));
    diff_std_final = sqrt(overall_diff_avg/(_exact_diff_count-1));

    TLOG_F(b_log::TEST,
        date, 
        SRC_FILE, 
        "[OVERALL END_TIME DIFFS]: TOTAL JOBS = %d, TOTAL DIFFS = %d, MINIMUM = %.15f, MAXIMUM = %.15f || [ALL JOBS]: AVG = %.15f, STD = %.15f || [DIFFS ONLY]: AVG = %.15f, STD = %.15f", 
        _workload->nb_jobs(), _exact_diff_count, overall_min, overall_max, overall_job_avg, job_std_final, overall_diff_avg, diff_std_final
    );

}