/*
 * sensor_analysis.c
 *
 * A Python C extension module that performs statistical operations
 * on collections of floating-point sensor readings entirely in C,
 * for use by a smart-agriculture monitoring platform.
 *
 * Exposed functions:
 *   average(data)            -> float
 *   range_value(data)        -> float
 *   variance(data)           -> float   (sample variance, N-1 denominator)
 *   count_above(data, limit) -> int
 *   statistics(data)         -> dict {samples, average, minimum, maximum}
 *
 * Design notes (see also the accompanying documentation comments
 * inline with each function):
 *   - All numeric work is done with C `double` values.
 *   - Input Python objects are converted to C doubles one element at
 *     a time using PyFloat_AsDouble()/PyLong_AsDouble()-style access;
 *     no bulk buffer is allocated on the heap for this, so there is
 *     NO additional dynamic memory allocation beyond the fixed-size
 *     local C variables already on the stack (accumulators, min/max,
 *     etc.) — satisfying the "avoid unnecessary allocation" requirement.
 *   - PyList_GET_ITEM / PySequence_Fast are used for O(1) element
 *     access instead of repeatedly calling PySequence_GetItem, which
 *     would create/destroy a temporary reference each time.
 */

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <math.h>

/* ---------------------------------------------------------------
 * Helper: validate and expose a Python list/tuple as a fast sequence
 * of C doubles. Returns a new reference to a PySequence_Fast object
 * (must be Py_DECREF'd by the caller) or NULL with an exception set.
 * This does not allocate any raw C buffer for the data itself: the
 * PySequence_Fast object simply gives us a borrowed array of the
 * existing Python objects' pointers to index into directly.
 * ------------------------------------------------------------- */
static PyObject *get_fast_sequence(PyObject *data) {
    if (!PyList_Check(data) && !PyTuple_Check(data)) {
        PyErr_SetString(PyExc_TypeError,
            "data must be a list or tuple of numeric values");
        return NULL;
    }
    PyObject *fast = PySequence_Fast(data, "expected a list or tuple");
    if (fast == NULL) {
        return NULL; /* PySequence_Fast already set an exception */
    }
    return fast;
}

/* ---------------------------------------------------------------
 * Helper: extract the i-th element of a fast sequence as a C double.
 * Validates that the element is int or float. On failure, sets a
 * Python exception and returns 0 with *ok = 0.
 * ------------------------------------------------------------- */
static double item_as_double(PyObject *fast, Py_ssize_t i, int *ok) {
    PyObject *item = PySequence_Fast_GET_ITEM(fast, i); /* borrowed ref */
    if (!PyFloat_Check(item) && !PyLong_Check(item)) {
        PyErr_Format(PyExc_TypeError,
            "element at index %zd is not a number", i);
        *ok = 0;
        return 0.0;
    }
    double v = PyFloat_AsDouble(item); /* handles both int and float */
    if (v == -1.0 && PyErr_Occurred()) {
        *ok = 0;
        return 0.0;
    }
    *ok = 1;
    return v;
}

/* =================================================================
 * average(data) -> float
 * Formula: mean = (sum of x_i) / n
 * Time complexity: O(n)  (single pass)
 * Numerical notes: a straightforward running sum is used; for the
 * modest dataset sizes expected from IoT sensor batches this is
 * numerically adequate. (Kahan summation could be added for very
 * large n if higher precision were required.)
 * ================================================================= */
static PyObject *sensor_average(PyObject *self, PyObject *args) {
    PyObject *data;
    if (!PyArg_ParseTuple(args, "O", &data)) {
        return NULL;
    }
    PyObject *fast = get_fast_sequence(data);
    if (!fast) return NULL;

    Py_ssize_t n = PySequence_Fast_GET_SIZE(fast);
    if (n == 0) {
        Py_DECREF(fast);
        PyErr_SetString(PyExc_ValueError, "data must not be empty");
        return NULL;
    }

    double sum = 0.0;
    for (Py_ssize_t i = 0; i < n; i++) {
        int ok;
        double v = item_as_double(fast, i, &ok);
        if (!ok) { Py_DECREF(fast); return NULL; }
        sum += v;
    }
    Py_DECREF(fast);
    return PyFloat_FromDouble(sum / (double)n);
}

/* =================================================================
 * range_value(data) -> float
 * Formula: range = max(x) - min(x)
 * Time complexity: O(n)  (single pass, tracking running min/max)
 * ================================================================= */
static PyObject *sensor_range(PyObject *self, PyObject *args) {
    PyObject *data;
    if (!PyArg_ParseTuple(args, "O", &data)) {
        return NULL;
    }
    PyObject *fast = get_fast_sequence(data);
    if (!fast) return NULL;

    Py_ssize_t n = PySequence_Fast_GET_SIZE(fast);
    if (n == 0) {
        Py_DECREF(fast);
        PyErr_SetString(PyExc_ValueError, "data must not be empty");
        return NULL;
    }

    int ok;
    double v0 = item_as_double(fast, 0, &ok);
    if (!ok) { Py_DECREF(fast); return NULL; }
    double min_v = v0, max_v = v0;

    for (Py_ssize_t i = 1; i < n; i++) {
        double v = item_as_double(fast, i, &ok);
        if (!ok) { Py_DECREF(fast); return NULL; }
        if (v < min_v) min_v = v;
        if (v > max_v) max_v = v;
    }
    Py_DECREF(fast);
    return PyFloat_FromDouble(max_v - min_v);
}

/* =================================================================
 * variance(data) -> float   (sample variance)
 * Formula: variance = sum((x_i - mean)^2) / (n - 1)
 * Time complexity: O(n) for the mean pass + O(n) for the deviations
 *                  pass = O(n) overall (two passes, still linear).
 * Numerical notes: the two-pass "compute mean, then sum of squared
 * deviations" approach is preferred here over the single-pass
 * "sum of squares minus n*mean^2" shortcut, because the latter is
 * numerically unstable (catastrophic cancellation) for data with a
 * large mean relative to its spread — a realistic scenario for
 * temperature/humidity sensor readings.
 * Requires n >= 2 (sample variance is undefined for a single point).
 * ================================================================= */
static PyObject *sensor_variance(PyObject *self, PyObject *args) {
    PyObject *data;
    if (!PyArg_ParseTuple(args, "O", &data)) {
        return NULL;
    }
    PyObject *fast = get_fast_sequence(data);
    if (!fast) return NULL;

    Py_ssize_t n = PySequence_Fast_GET_SIZE(fast);
    if (n < 2) {
        Py_DECREF(fast);
        PyErr_SetString(PyExc_ValueError,
            "variance requires at least 2 data points");
        return NULL;
    }

    double sum = 0.0;
    for (Py_ssize_t i = 0; i < n; i++) {
        int ok;
        double v = item_as_double(fast, i, &ok);
        if (!ok) { Py_DECREF(fast); return NULL; }
        sum += v;
    }
    double mean = sum / (double)n;

    double sq_sum = 0.0;
    for (Py_ssize_t i = 0; i < n; i++) {
        int ok;
        double v = item_as_double(fast, i, &ok);
        if (!ok) { Py_DECREF(fast); return NULL; }
        double d = v - mean;
        sq_sum += d * d;
    }
    Py_DECREF(fast);
    return PyFloat_FromDouble(sq_sum / (double)(n - 1));
}

/* =================================================================
 * count_above(data, limit) -> int
 * Formula: count of x_i such that x_i > limit
 * Time complexity: O(n)
 * ================================================================= */
static PyObject *sensor_count_above(PyObject *self, PyObject *args) {
    PyObject *data;
    double limit;
    if (!PyArg_ParseTuple(args, "Od", &data, &limit)) {
        return NULL;
    }
    PyObject *fast = get_fast_sequence(data);
    if (!fast) return NULL;

    Py_ssize_t n = PySequence_Fast_GET_SIZE(fast);
    long count = 0;
    for (Py_ssize_t i = 0; i < n; i++) {
        int ok;
        double v = item_as_double(fast, i, &ok);
        if (!ok) { Py_DECREF(fast); return NULL; }
        if (v > limit) count++;
    }
    Py_DECREF(fast);
    return PyLong_FromLong(count);
}

/* =================================================================
 * statistics(data) -> dict {"samples", "average", "minimum", "maximum"}
 * Time complexity: O(n) (single pass tracking sum/min/max together)
 * ================================================================= */
static PyObject *sensor_statistics(PyObject *self, PyObject *args) {
    PyObject *data;
    if (!PyArg_ParseTuple(args, "O", &data)) {
        return NULL;
    }
    PyObject *fast = get_fast_sequence(data);
    if (!fast) return NULL;

    Py_ssize_t n = PySequence_Fast_GET_SIZE(fast);
    if (n == 0) {
        Py_DECREF(fast);
        PyErr_SetString(PyExc_ValueError, "data must not be empty");
        return NULL;
    }

    int ok;
    double v0 = item_as_double(fast, 0, &ok);
    if (!ok) { Py_DECREF(fast); return NULL; }

    double sum = v0, min_v = v0, max_v = v0;
    for (Py_ssize_t i = 1; i < n; i++) {
        double v = item_as_double(fast, i, &ok);
        if (!ok) { Py_DECREF(fast); return NULL; }
        sum += v;
        if (v < min_v) min_v = v;
        if (v > max_v) max_v = v;
    }
    Py_DECREF(fast);

    double avg = sum / (double)n;

    /* Py_BuildValue creates the required Python objects for us
     * ("n" = Py_ssize_t, "d" = double); no manual malloc needed. */
    return Py_BuildValue("{s:n, s:d, s:d, s:d}",
                          "samples", n,
                          "average", avg,
                          "minimum", min_v,
                          "maximum", max_v);
}

/* -----------------------------------------------------------------
 * Method table: maps Python-visible names to the C implementations.
 * --------------------------------------------------------------- */
static PyMethodDef SensorAnalysisMethods[] = {
    {"average", sensor_average, METH_VARARGS,
     "average(data) -> float: arithmetic mean of the readings."},
    {"range_value", sensor_range, METH_VARARGS,
     "range_value(data) -> float: max(data) - min(data)."},
    {"variance", sensor_variance, METH_VARARGS,
     "variance(data) -> float: sample variance (N-1 denominator)."},
    {"count_above", sensor_count_above, METH_VARARGS,
     "count_above(data, limit) -> int: count of readings > limit."},
    {"statistics", sensor_statistics, METH_VARARGS,
     "statistics(data) -> dict: samples, average, minimum, maximum."},
    {NULL, NULL, 0, NULL} /* sentinel */
};

/* -----------------------------------------------------------------
 * Module definition structure.
 * --------------------------------------------------------------- */
static struct PyModuleDef sensor_analysis_module = {
    PyModuleDef_HEAD_INIT,
    "sensor_analysis",
    "C extension module for high-performance sensor data statistics.",
    -1,
    SensorAnalysisMethods
};

/* -----------------------------------------------------------------
 * Module initialization function. Python calls this the first time
 * `import sensor_analysis` runs.
 * --------------------------------------------------------------- */
PyMODINIT_FUNC PyInit_sensor_analysis(void) {
    return PyModule_Create(&sensor_analysis_module);
}
