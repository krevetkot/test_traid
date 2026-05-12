savedcmd_dm_race_detector.mod := printf '%s\n'   dm_race_detector.o | awk '!x[$$0]++ { print("./"$$0) }' > dm_race_detector.mod
