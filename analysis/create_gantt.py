from evalys.jobset import JobSet
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
end_time = round(js.df.finish_time.max(),2)

ax = plt.gca()

xticks = ax.get_xticks()
xticks = np.insert(xticks, -1, end_time)

time_xticks = [str(pd.to_timedelta(x, unit="s")) for x in xticks]
time_xticks[-1] = ""
tmp_time = time_xticks[0]
time_xticks[0] = f"{tmp_time}\n[START TIME]"
tmp_time = time_xticks[-2]
time_xticks[-2] = f"{tmp_time}\n[END TIME]"

str_time_xticks = [x.replace(" days ", " days\n") for x in time_xticks]

ax.set_xticks(xticks, labels=str_time_xticks, fontsize=8)

yticks = ax.get_yticks()
max_nodes=js.df.workload_num_machines.max()
yticks= np.insert(yticks,-1, max_nodes)
ytick_labels = [str(round(y)) for y in yticks]
ytick_labels[-1] = ""
ax.set_yticks(yticks[1:], labels=ytick_labels[1:],fontsize=8)

plt.xlabel(f"Time (days h:m:s)", labelpad=5)
plt.ylabel("Nodes", labelpad=5)
plt.suptitle(f"[Experiment Folder]: {input_folder}")
plt.title(f"[Totals]: #Jobs = {js.df.jobID.count()}, #Nodes = {max_nodes}, #Success = {js.df.success.count()}, Time = {end_time} (sec)", fontsize=8, pad=10)

plt.tight_layout()
plt.savefig(fig_path,dpi=600)

print(f"xticks={xticks}\ntime_xticks={time_xticks}\nfmt_time_xticks={str_time_xticks},\nfinish_time= [{end_time}] => [{tmp_time}]\nmax_nodes={max_nodes}")
print(f'Figure saved to "{fig_path}"')
