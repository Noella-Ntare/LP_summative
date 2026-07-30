/*
 * program.c
 * A small sensor-reading statistics demo used for ELF structural analysis.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

/* Tracks how many readings have been generated during the program run. */
int g_reading_count = 0;

/* Fills the allocated buffer with a simple trend-based set of readings. */
void generate_readings(double *buffer, int n) {
    /* Populate the array with a repeating pattern for the demo. */
    for (int i = 0; i < n; i++) {
        buffer[i] = 20.0 + (i * 1.5) - (i % 3);
        g_reading_count++;
    }
}

/* Computes the arithmetic mean of the current reading set. */
double compute_average(double *buffer, int n) {
    double sum = 0.0;
    for (int i = 0; i < n; i++) {
        sum += buffer[i];
    }
    return sum / n;
}

/* Classifies the average reading as LOW, NORMAL, or HIGH. */
const char *classify_average(double avg) {
    if (avg < 22.0) {
        return "LOW";
    } else if (avg < 26.0) {
        return "NORMAL";
    } else {
        return "HIGH";
    }
}

int main(void) {
    int n = 10;

    /* Allocate heap memory for the sensor readings before processing them. */
    double *readings = (double *) malloc(n * sizeof(double));
    if (readings == NULL) {
        fprintf(stderr, "Memory allocation failed\n");
        return 1;
    }

    generate_readings(readings, n);

    double avg = compute_average(readings, n);
    const char *status = classify_average(avg);

    /* Use the standard math library to compute a simple derived value. */
    double spread = sqrt(avg);

    printf("Sensor Monitoring Report\n");
    printf("-------------------------\n");
    printf("Readings processed : %d\n", n);
    printf("Global counter      : %d\n", g_reading_count);
    printf("Average reading     : %.2f\n", avg);
    printf("Status              : %s\n", status);
    printf("sqrt(average)        : %.2f\n", spread);

    free(readings);
    return 0;
}
