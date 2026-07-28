"""
test_sensor_analysis.py

Exercises every function in the sensor_analysis C extension module,
including a boundary/invalid-input case.
"""

import sensor_analysis

# Sample IoT sensor data: soil moisture percentages
readings = [22.5, 23.1, 19.8, 25.0, 24.2, 20.5, 26.3, 21.9]

print("Sample sensor readings:", readings)
print()

avg = sensor_analysis.average(readings)
print(f"average()      -> {avg:.4f}")

rng = sensor_analysis.range_value(readings)
print(f"range_value()  -> {rng:.4f}")

var = sensor_analysis.variance(readings)
print(f"variance()     -> {var:.4f}")

above = sensor_analysis.count_above(readings, 23.0)
print(f"count_above(23.0) -> {above}")

stats = sensor_analysis.statistics(readings)
print(f"statistics()   -> {stats}")

print()
print("=== Boundary / invalid-input tests ===")

# 1. Empty dataset should raise ValueError
try:
    sensor_analysis.average([])
except ValueError as e:
    print(f"average([]) correctly raised ValueError: {e}")

# 2. Non-numeric element should raise TypeError
try:
    sensor_analysis.average([1.0, "bad", 3.0])
except TypeError as e:
    print(f"average() with bad element correctly raised TypeError: {e}")

# 3. Wrong container type (not list/tuple) should raise TypeError
try:
    sensor_analysis.average(42)
except TypeError as e:
    print(f"average(42) correctly raised TypeError: {e}")

# 4. Single-element dataset for variance (needs >= 2 points)
try:
    sensor_analysis.variance([5.0])
except ValueError as e:
    print(f"variance([5.0]) correctly raised ValueError: {e}")

# 5. Tuple input should also work (not just list)
tuple_result = sensor_analysis.average((10.0, 20.0, 30.0))
print(f"average() accepts tuples -> {tuple_result:.4f}")

print()
print("All tests completed.")
