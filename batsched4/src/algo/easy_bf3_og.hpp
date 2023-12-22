#pragma once

#include <list>
#include <algorithm>
#include "../isalgorithm.hpp"
#include "../json_workload.hpp"
#include "../locality.hpp"
#include "../schedule.hpp"
//added
#include "../machine.hpp"
#include "../batsched_tools.hpp"
#include <random>
// @note LH: testing macros
#define T_CSV_INSTANCE _testCSV
#define T_LOG_INSTANCE _testLog
#define MIN(a,b) (((a)<(b)) ? (a) : (b))

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

    // @note LH: decision function declaration
    void check_priority_job(const Job * priority_job, double date);
    void check_next_job(const Job * next_job, double date);
    void handle_scheduled_job(const Job * job, double date);
    void handle_finished_job(std::string job_id, double date);
    void max_heap(int size, int root);
    void schedule_heap_sort(int size);
   
protected:
    Schedule _schedule;
    bool _debug = false;
    std::string _output_folder;
    //added
    Queue * _reservation_queue=nullptr;
    b_log *_myBLOG;
    
    // @note LH: All easy_bf additions
    struct Scheduled_Job
    {
        std::string id;
        int requested_resources;
        double wall_time;
        double start_time;
        double run_time;
        double est_finish_time;
        IntervalSet allocated_machines;
    };
    std::vector<Scheduled_Job *> _scheduled_jobs;
    Scheduled_Job * _tmp_job = NULL;
    void log_queue(double date);
    void log_schedule(double date);
    void log_next_job(const Job * job, double date);
    void log_priority_job(const Job * job, double date);
    struct Priority_Job
    {
        std::string id;
        int requested_resources;
        int extra_resources;
        double shadow_time;
        double est_finish_time;
    };
    Priority_Job * _p_job = NULL;
    b_log *_testCSV;
    b_log *_testLog;
    IntervalSet _available_machines;
    int _nb_available_machines = -1;
    int _backfill_counter = 0;

    bool _can_run = false;
    bool _is_priority = false;

    double _overall_time = 0.0;
    double _decision_time = 0.0;
    double _begin_overall = 0.0;
    double _begin_decision = 0.0;
    double _end_overall = 0.0;
    double _end_decision = 0.0;
};
