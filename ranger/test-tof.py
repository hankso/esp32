import time
import VL53L0X

tof = VL53L0X.VL53L0X(i2c_bus=0, i2c_address=0x29)
tof.open()
tof.start_ranging(VL53L0X.Vl53l0xAccuracyMode.BETTER)

timing_us = max(tof.get_timing(), 20000)
print('Timing %d ms' % (timing_us / 1000))

for count in range(100):
    distance_mm = tof.get_distance()
    if distance_mm > 0:
        print('Distance %3d %d mm' % (count, distance_mm))
    time.sleep(timing_us / 1e6)

tof.stop_ranging()
tof.close()
