import os
import sys
import decimal
import numpy as np
import pandas as pd
import seaborn as sns
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import matplotlib.legend_handler as lh

pd.set_option("display.precision", 15)

def make_plots(in_df, out_path, ranges):
    size = len(ranges)
    df = in_df.loc[0:size]
    means = df.filter(['MEAN_NONZERO','MEAN_OVERALL'])
    stds = df.filter(['STD_NONZERO','STD_OVERALL'])
    xlabels = [str(t).replace(' ', '') for t in in_df.JOB_RANGE]

    sns.set_style("whitegrid")
    
    cmap = ['blue', 'red']#sns.color_palette("viridis", len(means.columns))
    ax = plt.gca()
    
    ax.plot(size, in_df.MEAN_NONZERO[size+1], marker="*", markersize=6, color=cmap[0], ls='none', label=f"MEAN_NONZERO")
    plt.errorbar(x=size, y=in_df.MEAN_NONZERO[size+1], yerr=in_df.STD_NONZERO[size+1], fmt='none', color=cmap[0], alpha=.3, capsize=5)

    ax.plot(size, in_df.MEAN_OVERALL[size+1], marker="*", markersize=6, color=cmap[1], ls='none', label=f"MEAN_OVERALL")
    plt.errorbar(x=size, y=in_df.MEAN_OVERALL[size+1], yerr=in_df.STD_NONZERO[size+1], fmt='none', color=cmap[1], alpha=.3, capsize=5)

    sns.lineplot(data=means, marker='o', palette=cmap, linestyle='-')
    plt.errorbar(x=means.index, y=means.MEAN_NONZERO, yerr=stds.STD_NONZERO, fmt='none', color=cmap[0], alpha=.3, capsize=5)
    plt.errorbar(x=means.index, y=means.MEAN_OVERALL, yerr=stds.STD_OVERALL, fmt='none', color=cmap[1], alpha=.3, capsize=5)
    
    ax.set_xticks(range(0,size+1))
    ax.set_xticklabels(xlabels,fontsize=6)
    ax.set_xlabel("Job Ranges")

    ylabels = ax.get_yticks()
    ax.set_yticks(ylabels[1:])
    ax.set_yticklabels(ylabels[1:],fontsize=6)
    #idx=ylabels.tolist().index(0)
    #ax.set_yticks(ylabels[idx:])
    #ax.set_yticklabels(ylabels[idx:],fontsize=6)
    #ax.set_ylim(ylabels[idx],ylabels[-1])
    ax.yaxis.set_major_formatter(ticker.ScalarFormatter(useMathText=True, useOffset=False))
    ax.set_ylabel("Mean Difference")

    h, l = ax.get_legend_handles_labels()
    ax.legend(loc='upper center', bbox_to_anchor=(0.5, -0.1), ncol=2, fancybox=True, labels=['Mean Nonzero', 'Mean Overall'], markerscale=1, handles=zip([h[0],h[1]], [h[2],h[3]]), handler_map={tuple: lh.HandlerTuple(None)})
    ax.set_title("Mean Floating-Point Differences")
    
    plt.tight_layout()
    fig_file = f"{out_path}/std_nonzero.png"
    plt.savefig(fig_file,dpi=900)
    print(f"Saved plot to: '{fig_file}'")    


def calc_metrics(in_df, out_path, ranges):
    metrics = {
        'JOB_RANGE':{},
        'MINIMUM':{},
        'MAXIMUM':{},
        'MEAN_NONZERO':{},
        'STD_NONZERO':{},
        'MEAN_OVERALL':{},
        'STD_OVERALL':{}
    }
    diff_df = in_df.END_TIME_DIFF
    n = len(ranges)
    for i in range(0,n):
        metrics['JOB_RANGE'][i]=ranges[i]
        sub_df = diff_df.loc[ranges[i][0]-1:ranges[i][1]-1]
        metrics['MINIMUM'][i]=sub_df[sub_df != 0].min()
        metrics['MAXIMUM'][i]=sub_df.max()
        metrics['MEAN_NONZERO'][i]=sub_df[sub_df != 0].mean()
        metrics['STD_NONZERO'][i]=sub_df[sub_df != 0].std()
        metrics['MEAN_OVERALL'][i]=sub_df.mean()
        metrics['STD_OVERALL'][i]=sub_df.std()
    metrics['JOB_RANGE'][n+1]=(0, diff_df.shape[0])
    metrics['MINIMUM'][n+1]=diff_df[diff_df != 0].min()
    metrics['MAXIMUM'][n+1]=diff_df.max()
    metrics['MEAN_NONZERO'][n+1]=diff_df[diff_df != 0].mean()
    metrics['STD_NONZERO'][n+1]=diff_df[diff_df != 0].std()
    metrics['MEAN_OVERALL'][n+1]=diff_df.mean()
    metrics['STD_OVERALL'][n+1]=diff_df.std()
    out_df = pd.DataFrame(metrics)
    csv_file = f"{out_path}/endtime_diff_metrics.csv"
    out_df.to_csv(csv_file, float_format='%.15f', index=False)
    print(f"Saved results csv to: '{csv_file}'")
    print(f"calc_metrics():\n{out_df}")
    return out_df


def get_ranges(in_df, rsize):
    count = in_df.shape[0]
    step = int(np.floor(count/rsize))
    rem = count%step
    ranges = [(x,(lambda: x+step, lambda:count)[x+step == count-rem]()) for x in range(0,count-rem,step)]
    print(f"get_ranges():\n\tJobs = {count}, step = {step}, remainder = {rem}")
    print(f"\tranges = {ranges}")
    return ranges


def read_data(in_file):
    in_df = pd.read_csv(in_file,float_precision='round_trip',usecols=['JOB_ID','QUEUE_SIZE','REAL_END_TIME','EST_END_TIME'])
    in_df['END_TIME_DIFF'] = in_df.apply(lambda x: np.fabs(x['REAL_END_TIME'] - x['EST_END_TIME']), axis=1)
    print(f"Read input csv from: '{in_file}'")
    print(f"read_data():\n{in_df}\n")
    return in_df


def error(etype, arg):
    match etype:
        case 1:
            msg = "Usage: python3 analyze_easybf3.py <path_to_experiment_output> <job_count> <range_size>"
        case 2:
            msg = f"Error: <path_to_csv> '{arg}' does not exist"
        case 3: 
            msg = f"Error: cannot find input csv file in path '{arg}'"
        case 3: 
            msg = f"Error: <step> '{arg}' must be an integer more than 1"
    print(msg)
    sys.exit(1)


def main():
    expe_path = f"{sys.argv[1]}/experiment_1/id_1/Run_1/output/expe-out"
    input_file = f"{expe_path}/out_endtime_diffs.csv"
    output_path = f"{expe_path}/analysis"

    if (len(sys.argv) != 3):
        error(1)
    elif not os.path.exists(sys.argv[1]):
        error(2, sys.argv[1])
    elif not os.path.exists(input_file):
        error(3, input_file)
    elif not isinstance(int(sys.argv[2]), int) or int(sys.argv[2]) < 2:
        error(4, sys.argv[2])
    elif not os.path.exists(output_path):
        os.makedirs(output_path)

    range_size = int(sys.argv[2])
    input_df = read_data(input_file)
    job_ranges = get_ranges(input_df, range_size)
    output_df = calc_metrics(input_df, output_path, job_ranges)
    make_plots(output_df, output_path, job_ranges)

main()