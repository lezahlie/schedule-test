#pragma once
#include <list>
#include "../isalgorithm.hpp"
#include "../json_workload.hpp"
#include "../locality.hpp"
#include "../schedule.hpp"
//added
#include "../machine.hpp"
#include "../batsched_tools.hpp"
#include <random>

// @todo remove after debugging
#define T_LOG_INSTANCE _logDebug

// @note LH: Added for logging time data
#define T_CSV_INSTANCE _logTime
#define SRC_FILE "easy_bf2"

class EasyBackfilling2 : public ISchedulingAlgorithm
{
public:
    EasyBackfilling2(Workload * workload, SchedulingDecision * decision, Queue * queue, ResourceSelector * selector,
                    double rjms_delay, rapidjson::Document * variant_options);
    virtual ~EasyBackfilling2();

    virtual void on_simulation_start(double date, const rapidjson::Value & batsim_event);

    virtual void on_simulation_end(double date);
    virtual void make_decisions(double date,
                                SortableJobOrder::UpdateInformation * update_info,
                                SortableJobOrder::CompareInformation * compare_info);

    void sort_queue_while_handling_priority_job(const Job * priority_job_before,
                                                const Job *& priority_job_after,
                                                SortableJobOrder::UpdateInformation * update_info,
                                                SortableJobOrder::CompareInformation * compare_info);

    //added
    void on_machine_instant_down_up(batsched_tools::KILL_TYPES forWhat,double date);
    void on_machine_down_for_repair(batsched_tools::KILL_TYPES forWhat,double date);
    //virtual void set_workloads(myBatsched::Workloads * w);
    virtual void set_machines(Machines *m);
    virtual void on_requested_call(double date,int id,  batsched_tools::call_me_later_types forWhat);

    // @todo remove after debugging
    void log_queue(double date);
    void log_schedule(double date);
    void log_priority_job(const Job * job, bool can_run, double date);
    void log_next_job(const Job * job, bool can_run, double date);

protected:
    Schedule _schedule;
    bool _debug = false;

    //added
    Queue * _reservation_queue=nullptr;
    std::string _output_folder;
    std::string _output_svg;
    long _svg_frame_start;
    long _svg_frame_end;
    long _svg_output_start;
    long _svg_output_end;

    Schedule::RESCHEDULE_POLICY _reschedule_policy;
    Schedule::IMPACT_POLICY _impact_policy;
    double _previous_date;
    std::vector<Schedule::ReservedTimeSlice> _saved_reservations;
    bool _killed_jobs = false;
    bool _need_to_send_finished_submitting_jobs = true;
    std::vector<std::string> _saved_recently_queued_jobs;
    std::vector<std::string> _saved_recently_ended_jobs;
    IntervalSet _recently_under_repair_machines;
    bool _checkpointing_on;
    bool _start_a_reservation=false;
    b_log *_myBLOG;
    std::map<std::string,batsched_tools::KILL_TYPES>_resubmitted_jobs;
    std::vector<std::pair<const Job *,batsched_tools::KILL_TYPES>>_resubmitted_jobs_released;
    
    std::vector<batsched_tools::KILL_TYPES> _on_machine_instant_down_ups;
    std::vector<batsched_tools::KILL_TYPES> _on_machine_down_for_repairs;
    // @note added for time analysis
    int _backfill_counter = 0;
    double _begin_overall = 0.0;
    double _decision_time = 0.0;
    double _begin_decision = 0.0;
    double _end_decision = 0.0;
    b_log *_logTime;

    // @todo LH: remove after debugging
    b_log *_logDebug;
};
