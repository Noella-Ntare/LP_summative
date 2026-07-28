/*
 * program.c
 * A small sensor-reading statistics demo used for ELF structural analysis.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

/* Global variable runs count of readings processed across all calls */
int g_reading_count = 0;

/* Function 1: fills the allocated buffer with sample sensor readings */
void generate_readings(double *buffer, int n) {
    /* Loop requirement */
    for (int i = 0; i < n; i++) {
        buffer[i] = 20.0 + (i * 1.5) - (i % 3);
        g_reading_count++;
    }
}

/* Function 2 that computes the arithmetic mean of the buffer */
double compute_average(double *buffer, int n) {
    double sum = 0.0;
    for (int i = 0; i < n; i++) {
        sum += buffer[i];
    }
    return sum / n;
}

/* Function 3 that classifies the average reading; contains the decision statement */
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

    /* Dynamic memory allocation, data stored in the allocated memory */
    double *readings = (double *) malloc(n * sizeof(double));
    if (readings == NULL) {
        fprintf(stderr, "Memory allocation failed\n");
        return 1;
    }

    generate_readings(readings, n);

    double avg = compute_average(readings, n);
    const char *status = classify_average(avg);

    /* C standard library calls: printf, sqrt */
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
