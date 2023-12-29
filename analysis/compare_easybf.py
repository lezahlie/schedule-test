import os
import sys
import numpy as np
import pandas as pd
import seaborn as sns
import matplotlib.pyplot as plt
import matplotlib.legend_handler as lh
import matplotlib.ticker as tick
pd.set_option("display.precision", 15) 


def make_plots(df1, df2, out_path):
    catagories = ['Overall_Time','Decision_Time','Total_Backfilled','Speedup']
    ptypes = ['Bar', 'Line']
    titles_a = ['Original EasyBF','Modified EasyBF']
    titles_b = ['Overall','Decision']
    size = df1.shape[0]
    for cat in catagories:

        if 'Speedup' in cat:
            df1 = df2 = calc_speedup(df1, df2, out_path)

        for pt in ptypes:    
            sns.set_style("whitegrid")
            fig, axes = plt.subplots(ncols = 2, layout='tight', sharex=True, figsize=(10,4))

            for i, ax in enumerate(axes):

                df = (df2, df1) [i == 0]
                ycol = (cat, f"{titles_b[i]}_{cat}")['Speedup' in cat]
                title = (f"{titles_a[i]}: {cat}", f"{titles_b[i]}: {cat}")['Speedup' in cat]

                if 'Line' == pt:
                    sns.lineplot(data=df, x=df.Total_Jobs, y=df[ycol], marker='o',
                                    hue=df.Total_Machines, palette="bright", ax=ax)
                    xticks = df.Total_Jobs.unique()
                    str_xticks = [('%.0fk')%(int(x)*1e-3) for x in xticks]
                elif 'Bar' == pt:
                    sns.barplot(data=df, x=df.Total_Jobs, y=df[ycol],
                                    hue=df.Total_Machines, palette="bright", ax=ax)
                    xticks = ax.get_xticks()
                    str_xticks = [('%.0fk')%(x*1e-3) for x in df.Total_Jobs.unique()]
                    

                ax.set_xticks(xticks,labels=str_xticks, fontsize=8)
                ax.set_xlabel("Job Size")
                ax.set_title(title)
                max = int(np.ceil(df[ycol].max()))
                
                if 'Time' in cat:
                    ax.set_ylim(0, max)
                    yticks = ax.get_yticks()
                    s = np.where(yticks == 0)[0]
                    if s.any(): 
                        yticks = yticks[s[0]:]
                    time_yticks = [str(pd.to_timedelta(y, unit="s")) for y in yticks]
                    str_yticks = [y.replace("0 days ", "") for y in time_yticks]
                    ax.set_yticks(yticks,labels=str_yticks, fontsize=8)
                    ax.set_ylabel("Time (h:m:s)")
                elif 'Speedup' in cat:
                    max = (max+1,max)[max%2==0]
                    step = (2, 3)[i==0]
                    yticks = [x for x in range(1, max+step, step)]
                    ax.set_yticks(yticks)
                    ax.yaxis.set_major_formatter(tick.FuncFormatter(lambda y, pos: ('%.0fx')%(y)))
                    ax.set_ylabel("Speedup (x's)")
                elif "Backfilled" in cat:
                    step = max/size
                    ax.set_ylim(0, max+(step*2))
                    ax.yaxis.set_major_formatter(tick.FuncFormatter(lambda y, pos: ('%.0fk')%(y*1e-3)))
                    ax.set_ylabel("Backfilled (#Jobs)")
                
                
            fig_file = f"{out_path}/EasyBF_{cat}_{pt}.png"
            fig.savefig(fig_file, dpi=900)
            print(f"Saved {cat} {pt} plots to: '{fig_file}'")
            plt.cla()


def calc_speedup(df2, df3, out_path):
    speedup_df = df2.filter(['Total_Jobs','Total_Machines'])
    speedup_df['Overall_Speedup'] = df2.Decision_Time/df3.Decision_Time
    speedup_df['Decision_Speedup'] = df2.Overall_Time/df3.Overall_Time
    csv_file = f"{out_path}/EasyBF_Speedup_Data.csv"
    speedup_df.to_csv(csv_file, index=False)
    print(f"Saved Speedup data to: '{csv_file}'")
    return speedup_df


def read_data(infile_ebf2, infile_ebf3):
    df_ebf2 = pd.read_csv(infile_ebf2,float_precision='round_trip')
    df_ebf3 = pd.read_csv(infile_ebf3,float_precision='round_trip')
    return df_ebf2, df_ebf3


def main():
    expe_path = "../../experiments"
    if len(sys.argv) == 1 and not os.path.exists(expe_path):
        print(f"Error: default experiment path '{expe_path}' does not exist")
        sys.exit(1)
    elif len(sys.argv) == 2:
        if not os.path.exists(sys.argv[1]):
            print(f"Error: input argument <experiment_path> '{sys.argv[1]}' does not exist")
            sys.exit(1)
        else: 
            expe_path = sys.argv[1]
    elif len(sys.argv) > 2:
        print(f"Usage: python3 {sys.argv[0]} <experiment_path>\n<experiment_path> default: `/path/to/simulator/experiments`")
        sys.exit(1)

    easybf2_file = f"{expe_path}/easy_bf2_time_data.csv"
    easybf3_file = f"{expe_path}/easy_bf3_time_data.csv"
    output_path = f"{expe_path}/easybf_comparisons"

    if not os.path.exists(easybf2_file):
        print(f"Error: cannot find easy_bf2 time csv file '{easybf2_file}'")
        sys.exit(1)
    elif not os.path.exists(easybf3_file):
        print(f"Error: cannot find easy_bf3 time csv file '{easybf3_file}'")
        sys.exit(1)
    elif not os.path.exists(output_path):
        os.makedirs(output_path)
        print(f"Created new directory: '{output_path}'")
        
    df_v2, df_v3 = read_data(easybf2_file, easybf3_file)
    make_plots(df_v2, df_v3, output_path)

main()