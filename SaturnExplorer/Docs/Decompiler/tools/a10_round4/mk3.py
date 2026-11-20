import json, os
S = "/tmp/claude-0/-home-user-RomHackingTools/aae0ae36-3afa-5288-8786-5063bd965426/scratchpad/r4"
hd = S + "/hunks"
jobs = []
for num in ["0010", "0028", "0057", "0044", "0056", "0065", "0066", "0072", "0076"]:
    ks = sorted(int(f[6:8]) for f in os.listdir(hd) if f.startswith(num + ".h") and f.endswith(".rev.patch"))
    jobs.append({"id": num + "-all", "patch": num, "hunks": ks, "named": [], "runs": ["pc", "engine", "unit", "gen", "fault"]})
json.dump(jobs, open(S + "/jobs3.json", "w"))
print([(j["id"], len(j["hunks"])) for j in jobs])
