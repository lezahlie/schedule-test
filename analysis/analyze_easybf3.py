import os
import sys
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
import seaborn as sns
pd.set_option("display.precision", 15)


def calc_metrics(in_df, ranges):
    metrics = {
        'JOB_RANGE':{},
        'MIN_DIFF':{},
        'MAX_DIFF':{},
        'MEAN_DIFF':{},
        'STD_DIFF':{}
    }
    for i in range(0,len(ranges)):
        metrics['JOB_RANGE'][i]=ranges[i]
        sub_df = in_df.END_TIME_DIFF.loc[ranges[i][0]-1:ranges[i][1]-1]
        metrics['MIN_DIFF'][i]=sub_df[sub_df != 0].min()
        metrics['MAX_DIFF'][i]=sub_df.max()
        metrics['MEAN_DIFF'][i]=sub_df.mean()
        metrics['STD_DIFF'][i]=sub_df.std()
    # @todo add overall row + mean & std by diffs only + queue mean
    out_df = pd.DataFrame(metrics)
    print(out_df)


def get_ranges(in_df, rsize):
    count = in_df.shape[0]
    step = int(np.floor(count/rsize))
    rem = count%step
    ranges = [(x,(lambda: x+step, lambda:count)[(x+step == count-rem)]()) for x in range(0,count-rem,step)]
    print(f"step = {step}, rem = {rem}\nranges ={ranges}")
    return ranges


def read_data(input):
    in_df = pd.read_csv(input,float_precision='round_trip',usecols=['JOB_ID','QUEUE_SIZE','REAL_END_TIME','EST_END_TIME'])
    in_df['END_TIME_DIFF'] = in_df.apply(lambda x: np.fabs(x['REAL_END_TIME'] - x['EST_END_TIME']), axis=1)
    in_df.to_csv("tests/test_read_data.csv", float_format='%.15f')
    print(in_df)
    return in_df


def main():
    usage = "Usage: python3 analyze_easybf3.py <path_to_experiment_output> <job_count> <range_size>"
    if (len(sys.argv) != 3):
        print(usage)
        sys.exit(1)
    elif not os.path.exists(sys.argv[1]):
        print(f"Error: <path_to_csv> '{sys.argv[1]}' does not exist...")
        sys.exit(1)
    elif not isinstance(int(sys.argv[2]), int) or int(sys.argv[2]) < 2:
        print(f"Error: <step> '{sys.argv[2]}' must be an integer more than 1")
        sys.exit(1)
    else: 
        input_path = f"{sys.argv[1]}/experiment_1/id_1/Run_1/output/expe-out/out_endtime_diffs.csv"
        range_size = int(sys.argv[2])

        input_df = read_data(input_path)
        job_ranges = get_ranges(input_df, range_size)
        output_df = calc_metrics(input_df, job_ranges)



main()