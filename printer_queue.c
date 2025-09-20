#include <stdio.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <stdint.h> // Added for intptr_t

// Maximum number of jobs allowed in the job queue
#define MAX_JOBS 10
// Number of printer threads (change if you want multiple printers)
#define NUM_PRINTERS 1

// ================== New Definitions ==================
// Printer speed defines how many pages are printed per second.
#define PRINTER_SPEED 1          // pages per second
// Maximum allowed printing time per job in seconds.
#define PRINT_DEADLINE 22         // seconds allowed per job
// Maximum pages that can be printed within the deadline.
#define MAX_ALLOWED_PAGES (PRINTER_SPEED * PRINT_DEADLINE)
// ======================================================

#define NEWSPAPER 1
#define MAGAZINE  2
#define AD_BANNER 3

typedef struct {
    int job_id;
    int pages;
    int category;
    int priority;
} PrintJob;

PrintJob jobQueue[MAX_JOBS];
int jobSubCount = 0;
PrintJob readyQueue[MAX_JOBS + 10]; // Extra space for termination sentinels.
int readyCount = 0;

pthread_mutex_t print_mutex;
pthread_mutex_t queue_mutex;
sem_t printer;

pthread_mutex_t submission_mutex;
pthread_cond_t submission_cv;
int submitted = 0;

void safe_print(const char *format, ...) {
    va_list args;
    pthread_mutex_lock(&print_mutex);
    va_start(args, format);
    vprintf(format, args);
    fflush(stdout);
    va_end(args);
    pthread_mutex_unlock(&print_mutex);
}

const char* getTypeString(int category, int job_id) {
    if (job_id == -1)
        return "TERMINATE";
    static char buf[16];
    snprintf(buf, sizeof(buf), "%d", category);
    return buf;
}

void printJob(PrintJob job, char *dest, size_t destSize) {
    snprintf(dest, destSize, "(ID:%d, Pages:%d, Priority:%d, Type:%s)",
             job.job_id, job.pages, job.priority, getTypeString(job.category, job.job_id));
}

void displayQueues() {
    pthread_mutex_lock(&queue_mutex);
    safe_print("\n==================================================\n");
    safe_print("📋 Job Queue: [");
    char buf[128];
    for (int i = 0; i < MAX_JOBS; i++) {
        if (i < jobSubCount) {
            printJob(jobQueue[i], buf, sizeof(buf));
            safe_print("%s", buf);
        } else {
            safe_print("None");
        }
        if (i < MAX_JOBS - 1)
            safe_print(", ");
    }
    safe_print("]\n");
    safe_print("🚀 Ready Queue: [");
    for (int i = 0; i < readyCount; i++) {
        printJob(readyQueue[i], buf, sizeof(buf));
        safe_print("%s", buf);
        if (i < readyCount - 1)
            safe_print(", ");
    }
    safe_print("]\n");
    safe_print("==================================================\n");
    pthread_mutex_unlock(&queue_mutex);
}

void printSemaphoreStates() {
    int val;
    sem_getvalue(&printer, &val);
    safe_print("\n🔹 Semaphore States: printer = %d\n", val);
}

void sort_ready_queue() {
    int limit = readyCount;
    // Exclude a termination sentinel (if present as the last element).
    if (readyCount > 0 && readyQueue[readyCount - 1].job_id == -1)
        limit = readyCount - 1;
    for (int i = 0; i < limit - 1; i++) {
        for (int j = 0; j < limit - i - 1; j++) {
            if (readyQueue[j].priority > readyQueue[j + 1].priority ||
               (readyQueue[j].priority == readyQueue[j + 1].priority &&
                readyQueue[j].pages > readyQueue[j + 1].pages)) {
                PrintJob temp = readyQueue[j];
                readyQueue[j] = readyQueue[j + 1];
                readyQueue[j + 1] = temp;
            }
        }
    }
}

void* job_submission(void* arg) {
    int num_jobs;
    if (scanf("%d", &num_jobs) != 1) {
        safe_print("Invalid input for number of jobs.\n");
        return NULL;
    }
    for (int i = 0; i < num_jobs; i++) {
        PrintJob newJob;
        if (scanf("%d", &newJob.job_id) != 1 ||
            scanf("%d", &newJob.pages) != 1 ||
            scanf("%d", &newJob.category) != 1 ||
            scanf("%d", &newJob.priority) != 1) {
            safe_print("Invalid job input.\n");
            return NULL;
        }
        pthread_mutex_lock(&queue_mutex);
        if (jobSubCount < MAX_JOBS) {
            jobQueue[jobSubCount] = newJob;
            jobSubCount++;
            // Add job to readyQueue for processing.
            readyQueue[readyCount] = newJob;
            readyCount++;
        } else {
            safe_print("Job Queue is full! Cannot add Job ID: %d\n", newJob.job_id);
        }
        pthread_mutex_unlock(&queue_mutex);
        char jobInfo[128];
        printJob(newJob, jobInfo, sizeof(jobInfo));
        safe_print("\nJob Submitted: %s\n", jobInfo);
        displayQueues();
        // Post a semaphore for each submitted job.
        sem_post(&printer);
    }
    
    // Post one termination sentinel per printer thread.
    for (int j = 0; j < NUM_PRINTERS; j++) {
        pthread_mutex_lock(&queue_mutex);
        PrintJob term = { .job_id = -1, .pages = -1, .priority = -1, .category = -1 };
        readyQueue[readyCount++] = term;
        pthread_mutex_unlock(&queue_mutex);
        sem_post(&printer);
    }
    
    safe_print("\n[Main] Submitted %d jobs for processing...\n", num_jobs);
    
    pthread_mutex_lock(&submission_mutex);
    submitted = 1;
    pthread_cond_signal(&submission_cv);
    pthread_mutex_unlock(&submission_mutex);
    
    return NULL;
}

void* printer_function(void* arg) {
    // Use intptr_t for safe conversion between pointer and integer
    long printer_id = (long)(intptr_t)arg;
    while (1) {
        sem_wait(&printer);
        pthread_mutex_lock(&queue_mutex);
        sort_ready_queue();
        if (readyCount <= 0) {
            pthread_mutex_unlock(&queue_mutex);
            continue;
        }
        PrintJob jobToPrint = readyQueue[0];
        // Shift the queue.
        for (int i = 0; i < readyCount - 1; i++) {
            readyQueue[i] = readyQueue[i + 1];
        }
        readyCount--;
        pthread_mutex_unlock(&queue_mutex);
        
        if (jobToPrint.job_id == -1) {
            safe_print("\n🖨️ [Printer %ld] Received termination signal. Shutting down.\n", printer_id);
            break;
        }
        
        // Check if the job’s pages exceed what can be printed within the deadline.
        if (jobToPrint.pages > MAX_ALLOWED_PAGES) {
            safe_print("\n🖨️ [Printer %ld] Job ID: %d has %d pages which exceeds the maximum allowed (%d pages) based on printer speed (%d pages/sec) and deadline (%d sec). Job is terminated.\n",
                       printer_id, jobToPrint.job_id, jobToPrint.pages, MAX_ALLOWED_PAGES, PRINTER_SPEED, PRINT_DEADLINE);
            displayQueues();
            printSemaphoreStates();
            continue;  // Skip this job
        }
        
        // Simulate printing incrementally.
        safe_print("\n🖨️ [Printer %ld] Starting printing for Job ID: %d, Pages: %d, Type: %s, Priority: %d...\n",
                   printer_id, jobToPrint.job_id, jobToPrint.pages,
                   getTypeString(jobToPrint.category, jobToPrint.job_id),
                   jobToPrint.priority);
        int printed_pages = 0;
        int seconds_elapsed = 0;
        int total_print_time = (jobToPrint.pages + PRINTER_SPEED - 1) / PRINTER_SPEED;  // Ceiling division.
        while (printed_pages < jobToPrint.pages && seconds_elapsed < PRINT_DEADLINE) {
            sleep(1); // simulate printing for 1 second
            printed_pages += PRINTER_SPEED;
            seconds_elapsed++;
            safe_print("[Printer %ld] Job ID: %d Progress: %d/%d pages printed.\n",
                       printer_id, jobToPrint.job_id,
                       (printed_pages > jobToPrint.pages ? jobToPrint.pages : printed_pages),
                       jobToPrint.pages);
        }
        if (printed_pages < jobToPrint.pages) {
            safe_print("⚠️ [Printer %ld] Deadline exceeded! Job ID: %d did not finish within %d seconds and is terminated.\n",
                       printer_id, jobToPrint.job_id, PRINT_DEADLINE);
        } else {
            safe_print("✅ [Printer %ld] Job ID: %d printed successfully in %d seconds!\n", 
                       printer_id, jobToPrint.job_id, seconds_elapsed);
        }
        
        displayQueues();
        printSemaphoreStates();
    }
    return NULL;
}

int main() {
    pthread_t submission_thread, printer_threads[NUM_PRINTERS];
    
    pthread_mutex_init(&print_mutex, NULL);
    pthread_mutex_init(&queue_mutex, NULL);
    pthread_mutex_init(&submission_mutex, NULL);
    pthread_cond_init(&submission_cv, NULL);
    sem_init(&printer, 0, 0);
    
    // Create printer threads.
    for (long i = 0; i < NUM_PRINTERS; i++) {
        pthread_create(&printer_threads[i], NULL, printer_function, (void*)(intptr_t)i);
    }
    // Create the job submission thread.
    pthread_create(&submission_thread, NULL, job_submission, NULL);
    
    pthread_join(submission_thread, NULL);
    for (int i = 0; i < NUM_PRINTERS; i++) {
        pthread_join(printer_threads[i], NULL);
    }
    
    sem_destroy(&printer);
    pthread_mutex_destroy(&print_mutex);
    pthread_mutex_destroy(&queue_mutex);
    pthread_mutex_destroy(&submission_mutex);
    pthread_cond_destroy(&submission_cv);
    
    return 0;
}
