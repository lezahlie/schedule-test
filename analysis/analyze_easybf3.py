import os
import sys
import numpy as np
import pandas as pd
import seaborn as sns
import matplotlib.pyplot as plt
import matplotlib.legend_handler as lh
import matplotlib.ticker as tick
pd.set_option("display.precision", 15) 

def plot_minmax(in_df, out_path, ranges):
    size = len(ranges)
    df = in_df.loc[0:size]
    labels = ['Maximum', 'Minimum']
    minmax = df.filter(labels)
    xlabels = [str(t).replace(' ', '') for t in in_df.Job_Range]
    sns.set_style("whitegrid")
    cmap = ['red', 'blue']
    fig, axes = plt.subplots(nrows = 2, layout='tight')

    for i, ax in enumerate(axes):
        ax.plot(size, in_df[labels[i]][size+1], marker="*", markersize=6, color=cmap[i], ls='none', label=labels[i])
        sns.scatterplot(data=minmax[labels[i]], color=cmap[i], label = labels[i], ax=ax)

        ax.set_xticks(range(0,size+1))
        ax.set_xticklabels(xlabels,fontsize=6, rotation=40)
        ax.set_xlabel("Ranges (Jobs)")

        ylabels = ax.get_yticks()
        idx = np.where(ylabels >= 0)[0][0]
        ax.set_yticks(ylabels[idx:])
        ax.set_yticklabels(ylabels[idx:],fontsize=6)
        
        if(labels[i]=='Maximum'):
            ax.yaxis.set_major_formatter(tick.FuncFormatter(lambda x, pos: ('%.2f')%(x*1e6)))
            ax.set_ylabel("Microseconds (E-6)")
        else:
            ax.yaxis.set_major_formatter(tick.FuncFormatter(lambda x, pos: ('%.1f')%(x*1e9)))
            ax.set_ylabel("Nanoseconds (E-9)")
        ax.set_title(f"{labels[i]} End-Time Floating-Point Differences")

        h, l = ax.get_legend_handles_labels()
        ax.legend(labels=[labels[i]], loc='upper left', ncol=2, fancybox=True, markerscale=1,
                handles=[(h[0],h[1])], handler_map={tuple: lh.HandlerTuple(None)})
    
    fig_file = f"{out_path}/minmax_difference.png"
    plt.savefig(fig_file,dpi=900)
    print(f"Saved plot to: '{fig_file}'")


def plot_mean(in_df, out_path, ranges):
    size = len(ranges)
    df = in_df.loc[0:size]
    means = df.filter(['Mean_Nonzero','Mean_Overall'])
    stds = df.filter(['Std_Nonzero','Std_Overall'])
    cmap = ['red', 'blue']
    xlabels = [str(t).replace(' ', '') for t in in_df.Job_Range]
    sns.set_style("whitegrid")
    
    ax = plt.subplot()
    ax.plot(size, in_df.Mean_Nonzero[size+1], marker="*", markersize=6, color=cmap[0], ls='none', label=f"Mean_Nonzero")
    ax.errorbar(x=size, y=in_df.Mean_Nonzero[size+1], yerr=in_df.Std_Nonzero[size+1], fmt='none', color=cmap[0], alpha=.3, capsize=5)
    ax.plot(size, in_df.Mean_Overall[size+1], marker="*", markersize=6, color=cmap[1], ls='none', label=f"Mean_Overall")
    plt.errorbar(x=size, y=in_df.Mean_Overall[size+1], yerr=in_df.Std_Nonzero[size+1], fmt='none', color=cmap[1], alpha=.3, capsize=5)
    sns.lineplot(data=means, marker='o', palette=cmap)
    ax.errorbar(x=means.index, y=means.Mean_Nonzero, yerr=stds.Std_Nonzero, fmt='none', color=cmap[0], alpha=.3, capsize=5)
    ax.errorbar(x=means.index, y=means.Mean_Overall, yerr=stds.Std_Overall, fmt='none', color=cmap[1], alpha=.3, capsize=5)

    ax.set_xticks(range(0,size+1))
    ax.set_xticklabels(xlabels,fontsize=6, rotation=40)
    ax.set_xlabel("Ranges (Jobs)")
    
    ylabels = ax.get_yticks()
    ax.set_yticks(ylabels[1:-1])
    ax.set_yticklabels(ylabels[1:-1],fontsize=6)
    ax.yaxis.set_major_formatter(tick.FuncFormatter(lambda x, pos: ('%.2f')%(x*1e6)))
    ax.set_ylabel("Microseconds (E-6)")

    h, l = ax.get_legend_handles_labels()
    ax.legend(loc='lower left', ncol=2, 
            fancybox=True, labels=['Mean Nonzero', 'Mean Overall'], markerscale=1, 
            handles=zip([h[0],h[1]], [h[2],h[3]]), handler_map={tuple: lh.HandlerTuple(None)})
    ax.set_title("Mean End-Time Floating-Point Differences")

    plt.tight_layout()
    fig_file = f"{out_path}/mean_difference.png"
    plt.savefig(fig_file,dpi=900)
    print(f"Saved plot to: '{fig_file}'")
    plt.cla()

def calc_metrics(in_df, out_path, ranges):
    metrics = {
        'Job_Range':{},
        'Minimum':{},
        'Maximum':{},
        'Mean_Nonzero':{},
        'Std_Nonzero':{},
        'Mean_Overall':{},
        'Std_Overall':{}
    }
    diff_df = in_df.END_TIME_DIFF
    n = len(ranges)
    for i in range(0,n):
        metrics['Job_Range'][i]=ranges[i]
        sub_df = diff_df.loc[ranges[i][0]-1:ranges[i][1]-1]
        metrics['Minimum'][i]=sub_df[sub_df != 0].min()
        metrics['Maximum'][i]=sub_df.max()
        metrics['Mean_Nonzero'][i]=sub_df[sub_df != 0].mean()
        metrics['Std_Nonzero'][i]=sub_df[sub_df != 0].std()
        metrics['Mean_Overall'][i]=sub_df.mean()
        metrics['Std_Overall'][i]=sub_df.std()
    metrics['Job_Range'][n+1]=(0, diff_df.shape[0])
    metrics['Minimum'][n+1]=diff_df[diff_df != 0].min()
    metrics['Maximum'][n+1]=diff_df.max()
    metrics['Mean_Nonzero'][n+1]=diff_df[diff_df != 0].mean()
    metrics['Std_Nonzero'][n+1]=diff_df[diff_df != 0].std()
    metrics['Mean_Overall'][n+1]=diff_df.mean()
    metrics['Std_Overall'][n+1]=diff_df.std()
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

def main():
    expe_path = f"{sys.argv[1]}/experiment_1/id_1/Run_1/output/expe-out"
    input_file = f"{expe_path}/out_endtime_diffs.csv"
    output_path = f"{expe_path}/analysis"
    if (len(sys.argv) != 3):
        print("Usage: python3 analyze_easybf3.py <path_to_experiment_output> <job_count> <range_size>")
        sys.exit(1)
    elif not os.path.exists(sys.argv[1]):
        print(f"Error: <path_to_csv> '{sys.argv[1]}' does not exist")
        sys.exit(1)
    elif not os.path.exists(input_file):
        print(f"Error: cannot find input csv file in path '{input_file}'")
        sys.exit(1)
    elif not isinstance(int(sys.argv[2]), int) or int(sys.argv[2]) < 2:
        print(f"Error: <step> '{sys.argv[2]}' must be an integer more than 1")
        sys.exit(1)
    elif not os.path.exists(output_path):
        os.makedirs(output_path)

    range_size = int(sys.argv[2])
    input_df = read_data(input_file)
    job_ranges = get_ranges(input_df, range_size)
    output_df = calc_metrics(input_df, output_path, job_ranges)
    plot_mean(output_df, output_path, job_ranges)
    plot_minmax(output_df, output_path, job_ranges)
main()