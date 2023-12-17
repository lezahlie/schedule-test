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
js.gantt(labeler = True)

end_time = round(js.df.finish_time.max(),6)
end_time_dhms = pd.to_timedelta(end_time, unit="s")
end_time_str = str(end_time_dhms).replace(" days ", " days\n")
max_nodes=js.df.workload_num_machines.max()

ax = plt.gca()

xticks = ax.get_xticks()

'''
#For smaller plots
print(f"xticks = {xticks}")
xticks = np.insert(xticks, -2, end_time)
print(f"xticks = {xticks}")
time_xticks = [str(pd.to_timedelta(x, unit="s")) for x in xticks]
time_xticks[-1] = time_xticks[-2] = ""
tmp_time = time_xticks[0]
time_xticks[0] = f"{tmp_time}\n[START TIME]"
tmp_time = time_xticks[-2]
time_xticks[-2] = f"{tmp_time}\n[END TIME]"

'''
time_xticks = [str(pd.to_timedelta(x, unit="s")) for x in xticks]
str_time_xticks = [x.replace(" days ", " days\n") for x in time_xticks]
ax.set_xticks(xticks, labels=str_time_xticks, fontsize=8)

'''
#For smaller plots
yticks = ax.get_yticks()
#yticks= np.insert(yticks,-1, max_nodes)
print(f"yticks = {yticks}")
ytick_labels = [str(round(y)) for y in yticks]
ytick_labels[-1] = ""
ax.set_yticks(yticks[1:], labels=ytick_labels[1:],fontsize=8)
'''

yticks = [y for y in range(0,max_nodes+1, 1)]
ytick_labels = [str(y) for y in yticks]
ax.set_yticks(yticks, labels=ytick_labels,fontsize=8)

ax.annotate(
            f"[End Time]\n{end_time_dhms}\n({end_time} s)", 
            xy=(end_time, max_nodes), 
            xytext=(end_time+((xticks[-1]-end_time)/5),max_nodes+.25), 
            arrowprops=dict(facecolor='black', shrink=0.05, width=2, headwidth=8, headlength=8),
            fontsize = 6
            )

plt.xlabel(f"Time (days h:m:s)", labelpad=8)
plt.ylabel("Nodes", labelpad=8)
plt.title(f"[Experiment Folder]: {input_folder}", pad = 10)

avg_tat = round(js.df.turnaround_time.mean(),6)
avg_run = round(js.df.execution_time.mean(),6)
avg_wait = round(js.df.waiting_time.mean(),6)
avg_tat_str = str(pd.to_timedelta(avg_tat, unit="s"))
avg_run_str = str(pd.to_timedelta(avg_run, unit="s"))
avg_wait_str = str(pd.to_timedelta(avg_wait, unit="s"))

plt.subplots_adjust(bottom=0.25) 

table_data = {
    "Total Nodes": max_nodes, 
    "Total Jobs":js.df.jobID.count(), 
    "Total Success": js.df.success.count(), 
    "Avg Run-time": f"{avg_run_str}\n({avg_run} s)", 
    "Avg Tat-time": f"{avg_tat_str}\n({avg_tat} s)", 
    "Avg Wait-time":  f"{avg_wait_str}\n({avg_wait} s)"
    }

col_labels = list(table_data.keys())
cell_text = []
for key in table_data.keys():
    cell_text += [table_data[key]]

ax.table(cellText=[cell_text], colLabels=col_labels, cellLoc='center', fontsize = 8, bbox=[0, -0.35, 1, 0.15])

plt.tight_layout()
plt.savefig(fig_path,dpi=900)

print(f"xticks={xticks}\ntime_xticks={time_xticks}\nfmt_time_xticks={str_time_xticks},\nfinish_time= [{end_time}] => [{end_time_dhms}]\nmax_nodes={max_nodes}")
print(f'Figure saved to "{fig_path}"')
