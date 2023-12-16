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
// @note LH: addd for testing merge
#include <unordered_map>

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
    void check_priority_job(const Job * next_job, bool is_priority, double date);
    void remove_scheduled_job(std::string job_id);
    void max_heap(int size, int root);
    void schedule_heap_sort(int size);


protected:
    Schedule _schedule;
    bool _debug = false;
    std::string _output_folder;
    //added
    Queue * _reservation_queue=nullptr;
    b_log *_myBLOG;

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
        IntervalSet allocated_machines;
    };

    b_log *_testCSV;
    std::vector<Scheduled_Job *> _scheduled_jobs;
    Scheduled_Job * _tmp_job = NULL;
    int _backfill_counter = 0;
    bool _can_run = false;
    bool _job_exists = false;
    bool _is_priority = false;
    // @note LH: merge additions
    // Machines currently available
    IntervalSet _available_machines;
    IntervalSet _allocated_machines;
    int _nb_available_machines = -1;
    //Allocations of running jobs
    
};
