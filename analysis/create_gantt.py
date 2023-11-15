from evalys.jobset import JobSet
from evalys import visu
import matplotlib.pyplot as plt
import matplotlib as mpl
import pandas as pd
import numpy as np
import sys
import os

prefix = "/home/lezahlie/src/current/simulator"
save_dir = f"{prefix}/gantt_charts"

if not os.path.exists(save_dir):
    os.makedirs(save_dir)
if len(sys.argv) == 4:
    input_folder = sys.argv[1]
    exp_folder = sys.argv[2]
    exp_num = sys.argv[3]
    fig_path = f"{save_dir}/{input_folder}_exp-{exp_num}.png"
else:
    print("usage: python3 path/to/create_plots.py <experiment folder> <experiment #")

js = JobSet.from_csv(f'/home/lezahlie/src/current/simulator/experiments/{input_folder}/{exp_folder}/experiment_{exp_num}/id_1/Run_1/output/expe-out/out_jobs.csv')
print(js.df.describe())

mpl.rcParams['figure.figsize'] = 10,5

js.gantt(labeler = False)
end_time_str = str(pd.to_timedelta(round(js.df.finish_time.max(),2), unit='s'))

ax = plt.gca()

xticks = ax.get_xticks()
time_xticks = [str(pd.to_timedelta(x, unit="s")) for x in xticks]
fmt_time_xticks = [x.replace(" days ", " days\n") for x in time_xticks]

print(f"xticks={xticks}\ntime_xticks={time_xticks}\nfmt_time_xticks={fmt_time_xticks}")

ax.set_xticks(xticks, labels=fmt_time_xticks, fontsize=8)
plt.xlabel(f"Time (days h:m:s)", labelpad=10)
plt.ylabel("Nodes", labelpad=10)
plt.suptitle(input_folder)
plt.title(f"Last Job End Time: [{end_time_str}]", fontsize=10, pad=10)

plt.tight_layout()
plt.savefig(fig_path,dpi=600)

print(f'Figure saved to "{fig_path}"')
