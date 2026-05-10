echo "Starting script test..."
ls -l
echo "Sleeping for 2 seconds..."
sleep 2
echo "Done sleeping!"
./crash_ptr
echo "This line should run unless --stop is provided."
