from setuptools import setup, Extension

sensor_analysis_module = Extension(
    "sensor_analysis",
    sources=["sensor_analysis.c"],
    extra_compile_args=["-O2"],
)

setup(
    name="sensor_analysis",
    version="1.0",
    description="C extension for high-performance IoT sensor data statistics",
    ext_modules=[sensor_analysis_module],
)
