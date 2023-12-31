#include <stdio.h>
#include <chrono>
#include <regex>
#include <thread>

#include <vector>
#include <unordered_map>
#include <fstream>
#include <set>
#include <utility>

#include <boost/program_options.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>

#include <rapidjson/document.h>

#include <rapidjson/writer.h>

#include <loguru.hpp>

#include "external/taywee_args.hpp"
#include <csignal>

// Added to get profiles into batsched but we get the whole workload

#include "external/batsched_profile.hpp"


#include "isalgorithm.hpp"
#include "decision.hpp"
#include "network.hpp"
#include "json_workload.hpp"
#include "pempek_assert.hpp"
#include "data_storage.hpp"
#include "batsched_tools.hpp"
#include "machine.hpp"


/*
#include "algo/crasher.hpp"
*/
#include "algo/easy_bf.hpp"
#include "algo/easy_bf2.hpp"
// @note LH including easy_bf3
#include "algo/easy_bf3.hpp"

/*
#include "algo/easy_bf_fast.hpp"
#include "algo/easy_bf_plot_liquid_load_horizon.hpp"
#include "algo/energy_bf.hpp"
#include "algo/energy_bf_dicho.hpp"
#include "algo/energy_bf_idle_sleeper.hpp"
#include "algo/energy_bf_monitoring_period.hpp"
#include "algo/energy_bf_monitoring_inertial_shutdown.hpp"
#include "algo/energy_bf_machine_subpart_sleeper.hpp"
#include "algo/energy_watcher.hpp"
#include "algo/filler.hpp"
#include "algo/fcfs_fast.hpp"
#include "algo/killer.hpp"
#include "algo/killer2.hpp"
#include "algo/random.hpp"
#include "algo/rejecter.hpp"
#include "algo/sleeper.hpp"
#include "algo/sequencer.hpp"
#include "algo/submitter.hpp"
#include "algo/wt_estimator.hpp"
*/
#include "algo/fcfs_fast.hpp"
#include "algo/conservative_bf.hpp"
#include "algo/easy_bf_fast2.hpp"
#include "algo/easy_bf_fast2_holdback.hpp"
#include "algo/fcfs_fast2.hpp"



using namespace std;
using namespace boost;

namespace myB = myBatsched;

namespace n = network;
namespace r = rapidjson;

void run(Network & n, ISchedulingAlgorithm * algo, SchedulingDecision &d,
         Workload &workload, bool call_make_decisions_on_single_nop = true);
void on_signal_checkpoint(int signum);
bool batsim_checkpoint = false;
int _checkpoint_signal = 35;

/** @def STR_HELPER(x)
 *  @brief Helper macro to retrieve the string view of a macro.
 */
#define STR_HELPER(x) #x

/** @def STR(x)
 *  @brief Macro to get a const char* from a macro
 */
#define STR(x) STR_HELPER(x)

/** @def BATSCHED_VERSION
 *  @brief What batsched --version should return.
 *
 *  It is either set by CMake or set to vUNKNOWN_PLEASE_COMPILE_VIA_CMAKE
**/
#ifndef BATSCHED_VERSION
    #define BATSCHED_VERSION vUNKNOWN_PLEASE_COMPILE_VIA_CMAKE
#endif

int main(int argc, char ** argv)
{
    
    const set<string> variants_set = {"conservative_bf", "crasher", "easy_bf","easy_bf2", "easy_bf3", "easy_bf_fast",
                                       "easy_bf_fast2","easy_bf_fast2_holdback",
                                      "easy_bf_plot_liquid_load_horizon",
                                      "energy_bf", "energy_bf_dicho", "energy_bf_idle_sleeper",
                                      "energy_bf_monitoring",
                                      "energy_bf_monitoring_inertial", "energy_bf_subpart_sleeper",
                                      "energy_watcher", "fcfs_fast",
                                      "fcfs_fast2",
                                      "filler", "killer", "killer2", "random", "rejecter",
                                      "sequencer", "sleeper", "submitter", "waiting_time_estimator"};
    const set<string> policies_set = {"basic", "contiguous"};
    const set<string> queue_orders_set = {"fcfs", "original_fcfs" ,"lcfs", "desc_bounded_slowdown", "desc_slowdown",
                                          "asc_size", "desc_size", "asc_walltime", "desc_walltime"};
    const set<string> verbosity_levels_set = {"debug", "info", "quiet", "silent"};

    const string variants_string = "{" + boost::algorithm::join(variants_set, ", ") + "}";
    const string policies_string = "{" + boost::algorithm::join(policies_set, ", ") + "}";
    const string queue_orders_string = "{" + boost::algorithm::join(queue_orders_set, ", ") + "}";
    const string verbosity_levels_string = "{" + boost::algorithm::join(verbosity_levels_set, ", ") + "}";

    ISchedulingAlgorithm * algo = nullptr;
    ResourceSelector * selector = nullptr;
    Queue * queue = nullptr;
    SortableJobOrder * order = nullptr;

    args::ArgumentParser parser("A Batsim-compatible scheduler in C++.");
    args::HelpFlag flag_help(parser, "help", "Display this help menu", {'h', "help"});
    args::CompletionFlag completion(parser, {"complete"});
    
    args::ValueFlag<double> flag_rjms_delay(parser, "delay", "Sets the expected time that the RJMS takes to do some things like killing a job", {'d', "rjms_delay"}, 0.0);
    args::ValueFlag<string> flag_selection_policy(parser, "policy", "Sets the resource selection policy. Available values are " + policies_string, {'p', "policy"}, "basic");
    args::ValueFlag<string> flag_socket_endpoint(parser, "endpoint", "Sets the socket endpoint.", {'s', "socket-endpoint"}, "tcp://*:28000");
    args::ValueFlag<string> flag_scheduling_variant(parser, "variant", "Sets the scheduling variant. Available values are " + variants_string, {'v', "variant"}, "filler");
    args::ValueFlag<string> flag_variant_options(parser, "options", "Sets the scheduling variant options. Must be formatted as a JSON object.", {"variant_options"}, "{}");
    args::ValueFlag<string> flag_variant_options_filepath(parser, "options-filepath", "Sets the scheduling variant options as the content of the given filepath. Overrides the variant_options options.", {"variant_options_filepath"}, "");
    args::ValueFlag<string> flag_queue_order(parser, "order", "Sets the queue order. Available values are " + queue_orders_string, {'o', "queue_order"}, "fcfs");
    args::ValueFlag<string> flag_verbosity_level(parser, "verbosity-level", "Sets the verbosity level. Available values are " + verbosity_levels_string, {"verbosity"}, "info");
    //args::ValueFlag<string> flag_svg_prefix(parser,"svg_prefix", "Sets the prefix for outputing svg files using Schedule.cpp",{"svg_prefix"},"/tmp/");
    args::ValueFlag<bool> flag_call_make_decisions_on_single_nop(parser, "flag", "If set to true, make_decisions will be called after single NOP messages.", {"call_make_decisions_on_single_nop"}, true);
    args::Flag flag_version(parser, "version", "Shows batsched version", {"version"});

    try
    {
        parser.ParseCLI(argc, argv);

        if (flag_rjms_delay.Get() < 0)
            throw args::ValidationError(str(format("Invalid '%1%' parameter value (%2%): Must be non-negative.")
                                            % flag_rjms_delay.Name()
                                            % flag_rjms_delay.Get()));

        if (queue_orders_set.find(flag_queue_order.Get()) == queue_orders_set.end())
            throw args::ValidationError(str(format("Invalid '%1%' value (%2%): Not in %3%")
                                            % flag_queue_order.Name()
                                            % flag_queue_order.Get()
                                            % queue_orders_string));

        if (variants_set.find(flag_scheduling_variant.Get()) == variants_set.end())
            throw args::ValidationError(str(format("Invalid '%1%' value (%2%): Not in %3%")
                                            % flag_scheduling_variant.Name()
                                            % flag_scheduling_variant.Get()
                                            % variants_string));

        if (verbosity_levels_set.find(flag_verbosity_level.Get()) == verbosity_levels_set.end())
            throw args::ValidationError(str(format("Invalid '%1%' value (%2%): Not in %3%")
                                            % flag_verbosity_level.Name()
                                            % flag_verbosity_level.Get()
                                            % verbosity_levels_string));
    }
    catch(args::Help&)
    {
        parser.helpParams.addDefault = true;
        printf("%s", parser.Help().c_str());
        return 0;
    }
    catch (args::Completion & e)
    {
        printf("%s", e.what());
        return 0;
    }
    catch(args::ParseError & e)
    {
        printf("%s\n", e.what());
        return 1;
    }
    catch(args::ValidationError & e)
    {
        printf("%s\n", e.what());
        return 1;
    }

    if (flag_version)
    {
        printf("%s\n", STR(BATSCHED_VERSION));
        return 0;
    }

    string socket_endpoint = flag_socket_endpoint.Get();
    string scheduling_variant = flag_scheduling_variant.Get();
    string selection_policy = flag_selection_policy.Get();
    string queue_order = flag_queue_order.Get();
    string variant_options = flag_variant_options.Get();
    string variant_options_filepath = flag_variant_options_filepath.Get();
    string verbosity_level = flag_verbosity_level.Get();
    //string svg_prefix = flag_svg_prefix.Get();

    double rjms_delay = flag_rjms_delay.Get();
    bool call_make_decisions_on_single_nop = flag_call_make_decisions_on_single_nop.Get();

    try
    {
        // Logging configuration
        if (verbosity_level == "debug")
            loguru::g_stderr_verbosity = loguru::Verbosity_1;
        else if (verbosity_level == "quiet")
            loguru::g_stderr_verbosity = loguru::Verbosity_WARNING;
        else if (verbosity_level == "silent")
            loguru::g_stderr_verbosity = loguru::Verbosity_OFF;
        else
            loguru::g_stderr_verbosity = loguru::Verbosity_INFO;

        // Workload creation
        Workload w;
        w.set_rjms_delay(rjms_delay);

        // Scheduling parameters
        SchedulingDecision decision;

        // Queue order
        if (queue_order == "fcfs")
            order = new FCFSOrder;
        else if (queue_order == "original_fcfs")
            order = new OriginalFCFSOrder;
        else if (queue_order == "lcfs")
            order = new LCFSOrder;
        else if (queue_order == "desc_bounded_slowdown")
            order = new DescendingBoundedSlowdownOrder(1);
        else if (queue_order == "desc_slowdown")
            order = new DescendingSlowdownOrder;
        else if (queue_order == "asc_size")
            order = new AscendingSizeOrder;
        else if (queue_order == "desc_size")
            order = new DescendingSizeOrder;
        else if (queue_order == "asc_walltime")
            order = new AscendingWalltimeOrder;
        else if (queue_order == "desc_walltime")
            order = new DescendingWalltimeOrder;
        order = new OriginalFCFSOrder;
        queue = new Queue(order);

        // Resource selector
        if (selection_policy == "basic")
            selector = new BasicResourceSelector;
        else if (selection_policy == "contiguous")
            selector = new ContiguousResourceSelector;
        else
        {
            printf("Invalid resource selection policy '%s'. Available options are %s\n", selection_policy.c_str(), policies_string.c_str());
            return 1;
        }

        // Scheduling variant options
        if (!variant_options_filepath.empty())
        {
            ifstream variants_options_file(variant_options_filepath);

            if (variants_options_file.is_open())
            {
                // Let's put the whole file content into one string
                variants_options_file.seekg(0, ios::end);
                variant_options.reserve(variants_options_file.tellg());
                variants_options_file.seekg(0, ios::beg);

                variant_options.assign((std::istreambuf_iterator<char>(variants_options_file)),
                                        std::istreambuf_iterator<char>());
            }
            else
            {
                printf("Couldn't open variants options file '%s'. Aborting.\n", variant_options_filepath.c_str());
                return 1;
            }
        }

        rapidjson::Document json_doc_variant_options;
        json_doc_variant_options.Parse(variant_options.c_str());
        if (!json_doc_variant_options.IsObject())
        {
            printf("Invalid variant options: Not a JSON object. variant_options='%s'\n", variant_options.c_str());
            return 1;
        }
        LOG_F(1, "variant_options = '%s'", variant_options.c_str());

        // Scheduling variant
        if (scheduling_variant == "easy_bf")
            algo = new EasyBackfilling(&w, &decision, queue, selector, rjms_delay, &json_doc_variant_options);
        else if (scheduling_variant == "easy_bf2")
            algo = new EasyBackfilling2(&w, &decision, queue, selector, rjms_delay, &json_doc_variant_options);
        // @note LH adding easy_bf3 class
        else if (scheduling_variant == "easy_bf3")
            algo = new EasyBackfilling3(&w, &decision, queue, selector, rjms_delay, &json_doc_variant_options);
        else if (scheduling_variant == "fcfs_fast")
            algo = new FCFSFast(&w, &decision, queue, selector, rjms_delay, &json_doc_variant_options);
        else if (scheduling_variant == "fcfs_fast2")
            algo = new FCFSFast2(&w, &decision, queue, selector, rjms_delay, &json_doc_variant_options);
        else if (scheduling_variant == "easy_bf_fast2")
            algo = new easy_bf_fast2(&w, &decision, queue, selector,rjms_delay, &json_doc_variant_options);
        else if (scheduling_variant == "easy_bf_fast2_holdback")
            algo = new easy_bf_fast2_holdback(&w, &decision, queue, selector,rjms_delay, &json_doc_variant_options);
        else if (scheduling_variant == "conservative_bf")
            algo = new ConservativeBackfilling(&w, &decision,queue,selector,rjms_delay,&json_doc_variant_options);
            //algo = new ConservativeBackfilling(&w, &decision, queue, selector, rjms_delay, svg_prefix, &json_doc_variant_options);

        /*
        else if (scheduling_variant == "easy_bf_fast")
            algo = new EasyBackfillingFast(&w, &decision, queue, selector, rjms_delay, &json_doc_variant_options);
        else if (scheduling_variant == "easy_bf_plot_liquid_load_horizon")
            algo = new EasyBackfillingPlotLiquidLoadHorizon(&w, &decision, queue, selector, rjms_delay, &json_doc_variant_options);
        else if (scheduling_variant == "energy_bf")
            algo = new EnergyBackfilling(&w, &decision, queue, selector, rjms_delay, &json_doc_variant_options);
        else if (scheduling_variant == "energy_bf_dicho")
            algo = new EnergyBackfillingDichotomy(&w, &decision, queue, selector, rjms_delay, &json_doc_variant_options);
        else if (scheduling_variant == "energy_bf_idle_sleeper")
            algo = new EnergyBackfillingIdleSleeper(&w, &decision, queue, selector, rjms_delay, &json_doc_variant_options);
        else if (scheduling_variant == "energy_bf_monitoring")
            algo = new EnergyBackfillingMonitoringPeriod(&w, &decision, queue, selector, rjms_delay, &json_doc_variant_options);
        else if (scheduling_variant == "energy_bf_monitoring_inertial")
            algo = new EnergyBackfillingMonitoringInertialShutdown(&w, &decision, queue, selector, rjms_delay, &json_doc_variant_options);
        else if (scheduling_variant == "energy_bf_subpart_sleeper")
            algo = new EnergyBackfillingMachineSubpartSleeper(&w, &decision, queue, selector, rjms_delay, &json_doc_variant_options);
        else if (scheduling_variant == "energy_watcher")
            algo = new EnergyWatcher(&w, &decision, queue, selector, rjms_delay, &json_doc_variant_options);
        else if (scheduling_variant == "fcfs")
            algo = new FCFS(&w, &decision, queue, selector, rjms_delay, &json_doc_variant_options);
        else if (scheduling_variant == "fcfs_fast")
            algo = new FCFSFast(&w, &decision, queue, selector, rjms_delay, &json_doc_variant_options);
        */

        /*
        if (scheduling_variant == "filler")
            algo = new Filler(&w, &decision, queue, selector, rjms_delay, &json_doc_variant_options);
        else if (scheduling_variant == "crasher")
            algo = new Crasher(&w, &decision, queue, selector, rjms_delay, &json_doc_variant_options);
        else if (scheduling_variant == "killer")
            algo = new Killer(&w, &decision, queue, selector, rjms_delay, &json_doc_variant_options);
        else if (scheduling_variant == "killer2")
            algo = new Killer2(&w, &decision, queue, selector, rjms_delay, &json_doc_variant_options);
        else if (scheduling_variant == "random")
            algo = new Random(&w, &decision, queue, selector, rjms_delay, &json_doc_variant_options);
        else if (scheduling_variant == "rejecter")
            algo = new Rejecter(&w, &decision, queue, selector, rjms_delay, &json_doc_variant_options);
        else if (scheduling_variant == "sequencer")
            algo = new Sequencer(&w, &decision, queue, selector, rjms_delay, &json_doc_variant_options);
        else if (scheduling_variant == "sleeper")
            algo = new Sleeper(&w, &decision, queue, selector, rjms_delay, &json_doc_variant_options);
        else if (scheduling_variant == "submitter")
            algo = new Submitter(&w, &decision, queue, selector, rjms_delay, &json_doc_variant_options);
        else if (scheduling_variant == "waiting_time_estimator")
            algo = new WaitingTimeEstimator(&w, &decision, queue, selector, rjms_delay, &json_doc_variant_options);
        */
        bool success = false;
        Network n;
        n.bind(socket_endpoint);
           LOG_F(1, "before run");


        // Run the simulation
        run(n, algo, decision, w, call_make_decisions_on_single_nop);
    }
    catch(const std::exception & e)
    {
        string what = e.what();

        if (what == "Connection lost")
        {
            LOG_F(ERROR, "%s", what.c_str());
        }
        else
        {
            LOG_F(ERROR, "%s", what.c_str());

            delete queue;
            delete order;

            delete algo;
            delete selector;

            throw;
        }
    }

    delete queue;
    delete order;

    delete algo;
    delete selector;

    return 0;
}
void on_signal_checkpoint(int signum)
{
    batsim_checkpoint=true;
}

void run(Network & n, ISchedulingAlgorithm * algo, SchedulingDecision & d,
         Workload & workload, bool call_make_decisions_on_single_nop)
{
    
    LOG_F(INFO,"line 371 main.cpp");
    bool simulation_finished = false;
    //just doing single workloads
    //if this changes uncomment this line and some others
    //myB::Workloads myWorkloads;
    // Redis creation
    RedisStorage redis;
    LOG_F(INFO,"here");
    bool redis_enabled = false;
    LOG_F(INFO,"here");
    algo->set_redis(&redis);
    LOG_F(INFO,"here");
    //LOG_F(INFO,"line 378 main.cpp");
    
    while (!simulation_finished)
    {
        
        LOG_F(INFO,"line 381 main.cpp");
        string received_message;
        
   
        try{
            n.read(received_message);
        } catch (zmq::error_t &ex)
        {
            //batsim_checkpoint = true;
            if (boost::trim_copy(received_message).empty())
                continue;
        }
        if (batsim_checkpoint)
        {
            algo->on_signal_checkpoint();
            batsim_checkpoint = false;
        }
        //LOG_F(INFO,"line 384 main.cpp");
        if (boost::trim_copy(received_message).empty())
            throw runtime_error("Empty message received (connection lost ?)");
        //ok we have a message, get and set real time for use with checkpointing batsim
        algo->set_real_time(chrono::_V2::system_clock::now());
        d.clear();
        //LOG_F(INFO,"line 389 main.cpp");
        r::Document doc;
        doc.Parse(received_message.c_str());

        double message_date = doc["now"].GetDouble();
        double current_date = message_date;
        bool requested_callback_received = false;
        //LOG_F(INFO,"line 396 main.cpp");
        // Let's handle all received events
        const r::Value & events_array = doc["events"];

        for (unsigned int event_i = 0; event_i < events_array.Size(); ++event_i)
        {
            LOG_F(INFO,"line 400 main.cpp");
            const r::Value & event_object = events_array[event_i];
            const std::string event_type = event_object["type"].GetString();
            current_date = event_object["timestamp"].GetDouble();
            const r::Value & event_data = event_object["data"];
            LOG_F(INFO,"DEBUG");
            //LOG_F(INFO,"line 405 main.cpp");
            if (event_type == "SIMULATION_BEGINS")
            {
                int signal = event_data["config"]["checkpoint-signal"].GetInt();
                LOG_F(INFO,"CHECKPOINT SIGNAL=%d",signal);
                //register signal for checkpointing
                if (signal != -1)
                    std::signal(signal,on_signal_checkpoint);
                std::string checkpoint_batsim = event_data["config"]["checkpoint-batsim-interval"]["raw"].GetString();
                if (checkpoint_batsim != "False")
                {
                        int total_seconds = event_data["config"]["checkpoint-batsim-interval"]["total_seconds"].GetInt();
                        std::string checkpoint_type = event_data["config"]["checkpoint-batsim-interval"]["type"].GetString();
                        algo->set_checkpoint_time(total_seconds,checkpoint_type);
                             
                }
                else
                    algo->set_checkpoint_time(0,"False");
                int nb_resources;
                // DO this for retrocompatibility with batsim 2 API
                if (event_data.HasMember("nb_compute_resources"))
                {
                    nb_resources = event_data["nb_compute_resources"].GetInt();
                }
                else
                {
                    nb_resources = event_data["nb_resources"].GetInt();
                }
                redis_enabled = event_data["config"]["redis-enabled"].GetBool();

                if (redis_enabled)
                {
                    string redis_hostname = event_data["config"]["redis-hostname"].GetString();
                    int redis_port = event_data["config"]["redis-port"].GetInt();
                    string redis_prefix = event_data["config"]["redis-prefix"].GetString();

                    redis.connect_to_server(redis_hostname, redis_port, nullptr);
                    redis.set_instance_key_prefix(redis_prefix);
                }
                //get the workloads
                
                
                /*for (auto &member : event_data["workloads"].GetObject())
                {
                        std::string workload_name = member.name.GetString();
                        std::string workload_filename = member.value.GetString();
                        myB::Workload * myWorkload= myB::Workload::new_static_workload(workload_name,event_data["workloads"][workload_name.c_str()].GetString());
                        myWorkload->_checkpointing_on = event_data["config"]["checkpointing_on"].GetBool();
                        myWorkload->_compute_checkpointing = event_data["config"]["compute_checkpointing"].GetBool();
                        myWorkload->_MTBF = event_data["config"]["MTBF"].GetDouble();
                        myWorkload->_SMTBF = event_data["config"]["SMTBF"].GetDouble();
                        myWorkload->_repair_time = event_data["config"]["repair_time"].GetDouble();

                        

                        r::Document myCopy;
                        myCopy.CopyFrom(event_data,myCopy.GetAllocator());
                        r::Value & temp = myCopy["jobs"];
                        r::Value & job_json = temp[workload_name.c_str()];
                        r::Value & temp2 = myCopy["profiles"];
                        r::Value &profile_json = temp2[workload_name.c_str()];
                        myWorkload->load_from_batsim(workload_filename,
                                                     job_json,
                                                     profile_json);
                        LOG_F(INFO,"line 468");
                        myWorkload->_host_speed = event_data["compute_resources"][0]["speed"].GetDouble();
                        //just doing single workloads
                        //if this changes uncomment this line
                        //myWorkloads.insert_workload(workload_name,myWorkload);
                        workload._myWorkload = myWorkload;
                }
                LOG_F(INFO,"line 472");
               /*
                JUST DOING SINGLE WORKLOADS 
                myWorkloads._checkpointing_on = event_data["config"]["checkpointing_on"].GetBool();
                myWorkloads._compute_checkpointing = event_data["config"]["compute_checkpointing"].GetBool();
                myWorkloads._MTBF = event_data["config"]["MTBF"].GetDouble();
                myWorkloads._SMTBF = event_data["config"]["SMTBF"].GetDouble();
                myWorkloads._repair_time = event_data["config"]["repair_time"].GetDouble();
                myWorkloads._fixed_failures = event_data["config"]["fixed_failures"].GetDouble();
                myWorkloads._host_speed = event_data["compute_resources"][0]["speed"].GetDouble();
                */
                Machines * machines = new Machines;
                LOG_F(INFO,"line 489");
                for(const rapidjson::Value & resource : event_data["compute_resources"].GetArray())
                {
                    machines->add_machine_from_json_object(resource);
                }
                algo->set_machines(machines);
                workload._checkpointing_on = event_data["config"]["checkpointing_on"].GetBool();
                workload._compute_checkpointing = event_data["config"]["compute_checkpointing"].GetBool();
                workload._checkpointing_interval = event_data["config"]["checkpointing_interval"].GetDouble();
                workload._MTBF = event_data["config"]["MTBF"].GetDouble();
                workload._SMTBF = event_data["config"]["SMTBF"].GetDouble();
                workload._repair_time = event_data["config"]["repair_time"].GetDouble();
                workload._fixed_failures = event_data["config"]["fixed_failures"].GetDouble();
                workload._host_speed = event_data["compute_resources"][0]["speed"].GetDouble();
                workload._seed_failures = event_data["config"]["seed-failures"].GetBool();
                workload._queue_depth = event_data["config"]["scheduler-queue-depth"].GetInt();
                workload._subtract_progress_from_walltime = event_data["config"]["subtract-progress-from-walltime"].GetBool();
                workload._seed_repair_time = event_data["config"]["seed-repair-time"].GetBool();
                workload._MTTR = event_data["config"]["MTTR"].GetDouble();
                const rapidjson::Value & Vstart_from_checkpoint = event_data["config"]["start-from-checkpoint"];
                workload.start_from_checkpoint = new batsched_tools::start_from_chkpt();
                LOG_F(INFO, "here");
                workload.start_from_checkpoint->started_from_checkpoint = Vstart_from_checkpoint["started_from_checkpoint"].GetBool();
                LOG_F(INFO, "here");
                workload.start_from_checkpoint->nb_folder = Vstart_from_checkpoint["nb_folder"].GetInt();
                LOG_F(INFO, "here");
                workload.start_from_checkpoint->nb_checkpoint = Vstart_from_checkpoint["nb_checkpoint"].GetInt();
                LOG_F(INFO, "here");
                workload.start_from_checkpoint->nb_previously_completed = Vstart_from_checkpoint["nb_previously_completed"].GetInt();
                LOG_F(INFO, "here");
                workload.start_from_checkpoint->nb_original_jobs = Vstart_from_checkpoint["nb_original_jobs"].GetInt();
                LOG_F(INFO, "here");
                
                LOG_F(INFO, "before set workloads");
                /*
                JUST DOING SINGLE WORKLOADS
                algo->set_workloads(&myWorkloads);
                */
            LOG_F(INFO, "after set workloads");
                d.set_redis(redis_enabled, &redis);

                algo->set_nb_machines(nb_resources);
                
                if (workload.start_from_checkpoint->started_from_checkpoint)
                    algo->on_start_from_checkpoint(current_date,event_data);
                else
                    algo->on_simulation_start(current_date, event_data);
            }
            else if (event_type == "SIMULATION_ENDS")
            {
                algo->on_simulation_end(current_date);
                simulation_finished = true;
            }
            else if (event_type == "JOB_SUBMITTED")
            {
                LOG_F(INFO,"DEBUG");
                string job_id = event_data["job_id"].GetString();
                 LOG_F(INFO,"DEBUG");
                if (redis_enabled)
                    workload.add_job_from_redis(redis, job_id, current_date);
                else
                    workload.add_job_from_json_object(event_data,job_id,current_date);
                 LOG_F(INFO,"DEBUG");
                algo->on_job_release(current_date, {job_id});
                 LOG_F(INFO,"DEBUG");
            }
            else if (event_type == "JOB_COMPLETED")
            {
                //LOG_F(INFO,"line 486 main.cpp");
                string job_id = event_data["job_id"].GetString();
                //LOG_F(INFO,"line 488 main.cpp");
                workload[job_id]->completion_time = current_date;
                //LOG_F(INFO,"line 490 main.cpp");
                // @note LH: adds jobs to _jobs_ended_recently
                algo->on_job_end(current_date, {job_id});
                LOG_F(INFO,"LH: JOB_ID[%s] ENDED @ %.15f", job_id.c_str(), current_date);
                //LOG_F(INFO,"line 492 main.cpp");
            }
            else if (event_type == "RESOURCE_STATE_CHANGED")
            {
                IntervalSet resources = IntervalSet::from_string_hyphen(event_data["resources"].GetString(), " ");
                string new_state = event_data["state"].GetString();
                algo->on_machine_state_changed(current_date, resources, std::stoi(new_state));
            }
            else if (event_type == "JOB_KILLED")
            {
                LOG_F(INFO,"DEBUG");
                const r::Value & job_msgs = event_data["job_msgs"];
                PPK_ASSERT_ERROR(event_data["job_msgs"].IsArray());
                LOG_F(INFO,"DEBUG");
                std::unordered_map<std::string,batsched_tools::Job_Message *> job_msgs_map;

                for (auto itr = job_msgs.Begin(); itr != job_msgs.End(); ++itr)
                {
                    LOG_F(INFO,"DEBUG");
                    batsched_tools::Job_Message * msg = new batsched_tools::Job_Message;
                    msg->id = (*itr)["id"].GetString();
                    LOG_F(INFO,"DEBUG");
                    msg->forWhat = static_cast<batsched_tools::KILL_TYPES>((*itr)["forWhat"].GetInt());
                    const r::Value & job_progress = (*itr)["job_progress"];
                    r::StringBuffer sb;
                    r::Writer<r::StringBuffer> writer(sb);
                    job_progress.Accept(writer);
                    msg->progress_str = sb.GetString();
                    
                    
                    LOG_F(INFO,"DEBUG");
                    LOG_F(INFO,"DEBUG");
                    //job_progress.CopyFrom( itr->value["job_progress"].,doc.GetAllocator());                    
                    r::Document d;
                    d.Parse(sb.GetString());
                    LOG_F(INFO,"DEBUG");

                    while( !(d.HasMember("progress")))
                    {
                        PPK_ASSERT_ERROR(d.HasMember("current_task"),"While traversing the job_progress rapidjson of a JOB_KILLED event there was no 'progress' or 'current_task'");
                        d["current_task"].Accept(writer);
                        
                        d.Parse(sb.GetString());
                    }
                    LOG_F(INFO,"DEBUG");
                    msg->progress = d["progress"].GetDouble();
                    job_msgs_map.insert(std::make_pair(msg->id,msg));
                }

                algo->on_job_killed(current_date, job_msgs_map);
            }
            else if (event_type == "REQUESTED_CALL")
            {
                LOG_F(INFO,"DEBUG");
                requested_callback_received = true;
                algo->on_requested_call(current_date,event_data["id"].GetInt(),(batsched_tools::call_me_later_types)event_data["forWhat"].GetInt());
            }
            else if (event_type == "ANSWER")
            {
                for (auto itr = event_data.MemberBegin(); itr != event_data.MemberEnd(); ++itr)
                {
                    string key_value = itr->name.GetString();

                    if (key_value == "consumed_energy")
                    {
                        double consumed_joules = itr->value.GetDouble();
                        algo->on_answer_energy_consumption(current_date, consumed_joules);
                    }
                    else
                    {
                        PPK_ASSERT_ERROR(false, "Unknown ANSWER type received '%s'", key_value.c_str());
                    }
                }
            }
            else if (event_type == "QUERY")
            {
                const r::Value & requests = event_data["requests"];

                for (auto itr = requests.MemberBegin(); itr != requests.MemberEnd(); ++itr)
                {
                    string key_value = itr->name.GetString();

                    if (key_value == "estimate_waiting_time")
                    {
                        const r::Value & request_object = itr->value;
                        string job_id = request_object["job_id"].GetString();
                        workload.add_job_from_json_object(request_object["job"], job_id, current_date);

                        algo->on_query_estimate_waiting_time(current_date, job_id);
                    }
                    else
                    {
                        PPK_ASSERT_ERROR(false, "Unknown QUERY type received '%s'", key_value.c_str());
                    }
                }
            }
            else if (event_type == "NOTIFY")
            {
                string notify_type = event_data["type"].GetString();

                if (notify_type == "no_more_static_job_to_submit")
                {
                    algo->on_no_more_static_job_to_submit_received(current_date);
                }
                else if (notify_type == "no_more_external_event_to_occur")
                {
                    algo->on_no_more_external_event_to_occur(current_date);
                }
                else if (notify_type == "event_machine_available")
                {
                    IntervalSet resources = IntervalSet::from_string_hyphen(event_data["resources"].GetString(), " ");
                    algo->on_machine_available_notify_event(current_date, resources);
                }
                else if (notify_type == "event_machine_unavailable")
                {
                    IntervalSet resources = IntervalSet::from_string_hyphen(event_data["resources"].GetString(), " ");
                    algo->on_machine_unavailable_notify_event(current_date, resources);
                }
                else if (notify_type == "myKillJob")
                {
                    
                        algo->on_myKillJob_notify_event(current_date);
                    
                }
                else if (notify_type == "job_fault")
                {
                    LOG_F(INFO,"main.cpp notify_type==jobfault");
                    std::string job = event_data["job"].GetString();
                    algo->on_job_fault_notify_event(current_date,job);
                }
                else if (notify_type == "batsim_metadata")
                {
                    
                    std::string json_desc = event_data["metadata"].GetString();
                    LOG_F(INFO,"batsim_meta: %s",json_desc.c_str());
                    //LOG_F(INFO,"line 599 main.cpp");
                }
                else if (notify_type == "test")
                {
                        LOG_F(INFO,"test %f",current_date);
                }
                else
                {
                    throw runtime_error("Unknown NOTIFY type received. Type = " + notify_type);
                }

            }
            else
            {
                throw runtime_error("Unknown event received. Type = " + event_type);
            }
            //LOG_F(INFO,"line 615 main.cpp");
        }

        bool requested_callback_only = requested_callback_received && (events_array.Size() == 1);
        //LOG_F(INFO,"line 621 main.cpp");
        // make_decisions is not called if (!call_make_decisions_on_single_nop && single_nop_received)
        if (!(!call_make_decisions_on_single_nop && requested_callback_only))
        {
            SortableJobOrder::UpdateInformation update_info(current_date);
            LOG_F(1, "before make decisions");
            algo->make_decisions(message_date, &update_info, nullptr);
            algo->clear_recent_data_structures();
        }
        //LOG_F(INFO,"line 629 main.cpp");
        message_date = max(message_date, d.last_date());
        //LOG_F(INFO,"line 631 main.cpp");
        const string & message_to_send = d.content(message_date);
        //LOG_F(INFO,"line 633 main.cpp");
        n.write(message_to_send);
    }
}
