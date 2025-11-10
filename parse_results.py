import sys
import re

RootDir = "/local/v0/sendag/dpc3/ChampSim" # CHANGE THIS TO YOUR CHAMPSIM DIRECTORY
ScriptDir = "/local/v0/dpc3_traces"
ResultsDir = RootDir + "/results" # change this to your results directory

# Put Benchmark Traces into Array
TraceDir = ScriptDir
TraceFileName = "dpc3_all_simpoint.txt"
#TraceFileName = "dpc3_mcf_simpoint.txt"
TracesFile = open(TraceDir + "/" + TraceFileName, "r")
Traces = TracesFile.read().splitlines()
TracesFile.close()

# Determine Number of Prefetchers provided by Command Line
NumPrefetchers = len(sys.argv) - 1

IPC_OutputFile = open(ResultsDir + "/ipc.csv", "w")

# Print CSV Headers
IPC_OutputFile.write("Benchmark")
i = 1
while i <= NumPrefetchers:
    IPC_OutputFile.write("," + sys.argv[i])
    i = i+1
IPC_OutputFile.write("\n")

for Trace in Traces:
    # Output Trace Name (for CSV File)
    IPC_OutputFile.write(Trace)

    i = 1
    while i <= NumPrefetchers:
        Prefetcher = sys.argv[i]
        ResultFileName = ResultsDir  + "/" + Prefetcher + "-" + Trace + ".txt"
        ResultFile = open(ResultFileName, "r")

        # Get IPC for this Prefetcher Config
        IPC = 0.0
        for line in ResultFile.read().splitlines():
            ipc_check = re.search("CPU 0 cumulative IPC", line)
            if ipc_check:
                ipc_match = re.findall("\d.\d+", line)
                IPC = float(ipc_match[0])
                break;

        # Output Results to Respective Files
        IPC_OutputFile.write("," + str(IPC))

        # Close Result File
        ResultFile.close()

        # Next Prefetcher Config
        i = i + 1

    IPC_OutputFile.write("\n")

IPC_OutputFile.close()

