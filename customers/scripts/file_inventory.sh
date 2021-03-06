#!/bin/bash

declare -a buckets
range_counts=""
skip_empty="0"
SKIP_EMPTY="--skip-empty"
SKIP_EMPTY_SHORT="-skip-empty"
SKIP_EMPTY_ALT="-s"

brief=0

function update_bucket
{
   inside_old_range=${1}
   inside_count=${2}

   if [[ ${buckets[${inside_old_range}]} -gt 0 ]]
   then
       buckets[${inside_old_range}]=$(( ${buckets[${inside_old_range}]} + ${inside_count} ))
   else
       buckets[${inside_old_range}]=${inside_count}
   fi
}

function range_counter
{
   #       0                             1KB  2KB  4Kb  7KB  10KB  20KB  40KB  70KB  100KB  200KB  400KB  700KB  1MB     2MB     4MB     7MB     10MB     20MB     30MB      50MB    100MB     200MB     400MB     700MB     1GB        2GB        4GB        7GB        10GB        20GB        40GB        70GB
   ranges="0 10 20 40 70 100 200 400 700 1000 2000 4000 7000 10000 20000 40000 70000 100000 200000 400000 700000 1000000 2000000 4000000 7000000 10000000 20000000 40000000 70000000 100000000 200000000 400000000 700000000 1000000000 2000000000 4000000000 7000000000 10000000000 20000000000 40000000000 70000000000"

   range_counts=""
   old_range=0
   count=0

   ### Prints the nice footer
   if [[ "${1}" == "range_report" ]]
   then
      printf "\n  Ranges (in bytes)            Total File Count by Range\n"
      for range in ${ranges}
      do
         if [[ ${range} -eq 0 ]]
         then
            printf "%12d %24d\n" ${range} ${buckets[${range}]}
         else
            printf "%12d to %-12d %8d\n" $(( ${old_range} +1 )) ${range} ${buckets[${range}]}
         fi
         old_range=${range}
      done

      printf "%12d to %-12s %8d\n" $(( ${old_range} +1 )) "~" ${buckets[${range}]}

      return
   fi

   for range in ${ranges}
   do
      if [[ ${range} -eq 0 ]]
      then
         count=$( find ${1} -maxdepth 1 -type f -size ${range}c |wc -l )
      else
         count=$( find ${1} -maxdepth 1 -type f \( -size +${old_range}c -a -size -${range}c \) -o -size ${range}c |wc -l )
      fi

      if [[ ${count} -gt 0 ]]
      then
         update_bucket ${old_range} ${count}
      fi

      if [[ ${#range_counts} -gt 0 ]]
      then
         range_counts="${range_counts}:${count}"
      else
         range_counts="${count}"
      fi

      old_range=${range}
   done

   count=$( find ${1} -maxdepth 1 -type f -size +${old_range}c |wc -l )

   if [[ ${count} -gt 0 ]]
   then
      update_bucket ${old_range} ${count}
   fi

   range_counts="${range_counts}:${count}"
}

function help
{
   echo "Usage:  $0 [--skip-empty] [directory]"
   echo
   echo "    --skip-empty will not display directories with no files"
   echo "    directory, path to report on, defaults to './' (optional)"

   exit
}

[[ "${1}" == "-h" || "${1}" == "-help" || "${1}" == "--help" || "${1}" == "help" ]] && help

if [[ "${1}" == "${SKIP_EMPTY}" || "${1}" == "${SKIP_EMPTY_SHORT}" || "${1}" == "${SKIP_EMPTY_ALT}" ]]
then
   skip_empty=1
   search_path=${2-\.}
elif [[ "${2}" == "${SKIP_EMPTY}" || "${2}" == "${SKIP_EMPTY_SHORT}" || "${2}" == "${SKIP_EMPTY_ALT}" ]]
then
   skip_empty=1
   search_path=${1}
else
   search_path=${1-\.}
fi

[[ ! -d ${search_path} ]] && echo "directory path not found:  ${search_path}" && exit 1

dir_list=$( find ${search_path} -type d | sort )

printf " files    dirSz   smlFsz     lrgFsz   Ranged file Counts                      Path\n"

dir_count=0

for d in ${dir_list}
do
   dir_count=$(( ${dir_count} + 1 ))
   file_count=$( find ${d} -maxdepth 1 -type f |grep -v "\/\." |wc -l )

   [[ ${file_count} -eq 0 && ${skip_empty} -eq 1 ]] && continue

   directory_size=$( du -Sm --max=0 ${d} |cut -f1 )

   if [[ ${file_count} -gt 0 ]]
   then
      large_file=$( ls -lpSr ${d} |grep -v "/" |tail -1|awk '{print $5}' )
      small_file=$( ls -lpS ${d} |grep -v "/" |tail -1|awk '{print $5}' )
   else
      large_file="0"
      small_file="0"
   fi

   range_counter ${d}

   if [[ ${brief} -eq 1 ]]
   then
      echo -n "."
      continue
   fi

   printf "%6s %8s %8s %10s   %-39s %s \n" ${file_count} ${directory_size}"mb" ${small_file} ${large_file} ${range_counts} ${d}
done

total_file_count=0

for item in "${buckets[@]}"
do
   total_file_count=$(( ${total_file_count} + ${item} ))
done

printf "\nTotal Directories:  %d\n" ${dir_count}
printf "      Total Files:  %d\n" ${total_file_count}
printf "  Total Size (MB):  %d\n" $( du -sm ${search_path} | cut -f1 )

range_counter range_report
