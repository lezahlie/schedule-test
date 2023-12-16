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


class EasyBackfilling3 : public ISchedulingAlgorithm
{
public:
    EasyBackfilling3(Workload * workload, SchedulingDecision * decision, Queue * queue, ResourceSelector * selector,
                    double rjms_delay, rapidjson::Document * variant_options);
    virtual ~EasyBackfilling3();

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
    
    // @note LH: decision function declaration
    void check_priority_job(const Job * next_job, double date);
    void analyze_endtime_diffs(double date);

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

    // @note LH: Added for decision function
    struct Scheduled_Job
    {
        std::string id;
        int requested_resources;
        double wall_time;
        double start_time;
        double run_time;
        double est_finish_time;
        double real_finish_time;
    };

    //b_log *_testBLOG;
    b_log *_testCSV;
    std::vector<Scheduled_Job *> _scheduled_jobs;

    std::map<std::string,double> _finish_time_diff_map;
    std::vector<double> _avg_only_diffs_by_range;
    std::vector<double> _avg_all_jobs_by_range;
    std::vector<int> _diff_count_by_range;
    Scheduled_Job * _tmp_job = NULL;

    int _backfill_counter = 0;

    //bool _decision_exact = false;
    //int _exact_diff_count = 0;
    //double _finish_time_diff = 0.0;
    
};