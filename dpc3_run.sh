#!/bin/bash

if [ "$#" -ne 2 ]
then
    echo "Usage: ./run.sh <L1D pref> <Benchmark>"
    echo "Example: ./run.sh next_line all"
    echo "Example: ./run.sh mypref mcf"
  exit 0
fi

# File Configuration - You Need to Modify the BASE DIR
BASE_DIR="/home/calvin/Documents/uri/ele548/hw3/dpc3"
CHAMPSIM_DIR="${BASE_DIR}/ChampSim"              # Directory of ChampSim Framework
TRACE_DIR="${BASE_DIR}/dpc3_traces"                          # Directory Containing Traces
BUILD_SCRIPT="${CHAMPSIM_DIR}/build_champsim.sh"           # Script to Build Configuration (Use the one provided by the Framework)
RESULT_DIR="${CHAMPSIM_DIR}/results/"                      # Directory to Output Results to
L1D_PREF_NAME=`echo "$1"`

# Step 1: Get Traces
TRACE_ALL="dpc3_all_simpoint.txt"
TRACE_MCF="dpc3_mcf_simpoint.txt"

case "$2"
in
  "all") TRACE_LIST=${TRACE_ALL} ;;
  "mcf") TRACE_LIST=${TRACE_MCF} ;;
  *)     echo "Benchmark Config Options: all, mcf"
         exit 0                  ;;
esac

TRACES=$( cat "${TRACE_DIR}/${TRACE_LIST}" )

# Step 2: Set Configuration Options
L1D_PREF=${L1D_PREF_NAME}                                  # L1 Cache Prefetcher "e.g., next_line"
L2C_PREF="no"                                              # L2 Cache Prefetcher
LLC_PREF="no"                                              # Last Level Cache Prefetcher
BP="hashed_perceptron"                                     # Branch Predictor
REPL="lru"                                                 # Cache Replacement Policy
CORES="1"                                                  # Number of Cores
WARMUP_INSTR="50000000"                                    # Warmup Instructions
SIM_INSTR="200000000"                                      # Simulation Instructions
BINARY="${CHAMPSIM_DIR}/bin/${BP}-${L1D_PREF}-${L2C_PREF}-${LLC_PREF}-${REPL}-${CORES}core"

# Step 3: Build Configuration
( ${BUILD_SCRIPT} ${BP} ${L1D_PREF} ${L2C_PREF} ${LLC_PREF} ${REPL} ${CORES} > /dev/null )
#exit
# Step 4: Run Configuration on Traces
for trace in ${TRACES}
do
  ( ${BINARY} -warmup_instructions ${WARMUP_INSTR} -simulation_instructions ${SIM_INSTR} -traces ${TRACE_DIR}/${trace} > "${RESULT_DIR}/${L1D_PREF}-${L2C_PREF}-${LLC_PREF}-${trace}.txt" & )
done
