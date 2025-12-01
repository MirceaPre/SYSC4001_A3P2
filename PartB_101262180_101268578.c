#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/wait.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>

#define SEM_RUBRIC 0
#define SEM_EXAM 1
#define NUM_STUDENTS 20
#define NUM_QUESTIONS 5
#define EXAM_DIR "exams/"
#define RUBRIC_FILE "rubric.txt"
#define BUFFER_SIZE 32

typedef struct shared_memory {
    char rubric[NUM_QUESTIONS];
    char student_id[5];
    bool question_marked[NUM_QUESTIONS]; 
    int current_exam;
} shared_memory;

#ifndef __APPLE__
union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};
#endif

void semaphore_p(int sem_id, int sem_num) {
    struct sembuf sem_b;
    sem_b.sem_num = sem_num;
    sem_b.sem_op = -1;
    sem_b.sem_flg = SEM_UNDO;
    if (semop(sem_id, &sem_b, 1) == -1) {
        perror("Error: semaphore_p failed!");
        exit(EXIT_FAILURE);
    }
}

void semaphore_v(int sem_id, int sem_num) {
    struct sembuf sem_b;
    sem_b.sem_num = sem_num;
    sem_b.sem_op = 1;
    sem_b.sem_flg = SEM_UNDO;
    if (semop(sem_id, &sem_b, 1) == -1) {
        perror("Error: semaphore_v failed!");
        exit(EXIT_FAILURE);
    }
}

void load_rubric(shared_memory *shm) {
    FILE *fp = fopen(RUBRIC_FILE, "r");
    if (!fp) {
        perror("Error: Failed to open rubric.txt!");
        return;
    }
    char line[BUFFER_SIZE];
    int question;
    char text;

    while (fgets(line, sizeof(line), fp)) {
        if (sscanf(line, "%d,%c", &question, &text) == 2) {
            if (question <= NUM_QUESTIONS) {
                shm->rubric[question - 1] = text;
            }
        }
    }
    fclose(fp);
}

void save_rubric(shared_memory *shm) {
    FILE *fp = fopen(RUBRIC_FILE, "w");
    if (!fp) {
        perror("Error: Failed to open rubric.txt!");
        return;
    }


    for (int i = 0; i < NUM_QUESTIONS; i++) {
        fprintf(fp, "%d,%c\n", i + 1, shm->rubric[i]);
    }
    fclose(fp);
}

void load_exam(shared_memory *shm) {
    shm->current_exam++;
    int file_number = shm->current_exam;


    char filepath[BUFFER_SIZE];
    sprintf(filepath, "%sexam_%02d.txt", EXAM_DIR, file_number);

    FILE *fp = fopen(filepath, "r");
    if (!fp) return;

    char buffer[BUFFER_SIZE];
    if (fgets(buffer, sizeof(buffer), fp)) {
        buffer[strcspn(buffer, "\n")] = 0;
        strcpy(shm->student_id, buffer);
    }
    fclose(fp);

    for (int i = 0; i < NUM_QUESTIONS; i++) {
        shm->question_marked[i] = false;
    }

    printf("Loaded %s. New Student ID: %s\n", filepath, shm->student_id);
}

void ta_process(int num, int shmid, int semid) {
    srand(time(NULL) + getpid());

    shared_memory *shm = (shared_memory *)shmat(shmid, NULL, 0);
    if (shm == (void *)-1) {
        exit(1);
    }
    
    printf("TA %d - Started.\n", num);

    while (1) {
        semaphore_p(semid, SEM_EXAM);
        if (strcmp(shm->student_id, "9999") == 0) {
            semaphore_v(semid, SEM_EXAM);
            break; 
        }
        semaphore_v(semid, SEM_EXAM);

        for (int i = 0; i < NUM_QUESTIONS; i++) {
            usleep(500000 + (rand() % 500000));
            if ((rand() % 10) == 0) {
                semaphore_p(semid, SEM_RUBRIC);
                char old = shm->rubric[i];
                shm->rubric[i]++;
                printf("TA %d - Rubric change Q%d: %c -> %c\n", num, i+1, old, shm->rubric[i]);
                save_rubric(shm);
                semaphore_v(semid, SEM_RUBRIC);
            }
        }

        for (int i = 0; i < NUM_QUESTIONS; i++) {
            semaphore_p(semid, SEM_EXAM); 
            
            if (strcmp(shm->student_id, "9999") == 0) {
                semaphore_v(semid, SEM_EXAM);
                break;
            }
            
            if (!shm->question_marked[i]) {
                shm->question_marked[i] = true;
                printf("TA %d - Marking Student %s, Q%d\n", num, shm->student_id, i+1);
                
                semaphore_v(semid, SEM_EXAM); 
                
                usleep(1000000 + (rand() % 1000000));
                break;
            } else {
                semaphore_v(semid, SEM_EXAM); 
            }
        }

        semaphore_p(semid, SEM_EXAM); 
        
        if (strcmp(shm->student_id, "9999") != 0) {
            bool all_marked = true;
            for (int i = 0; i < NUM_QUESTIONS; i++) {
                if (!shm->question_marked[i]) {
                    all_marked = false;
                    break;
                }
            }

            if (all_marked) {
                printf("Student %s's exam marking complete. \n", shm->student_id);
                load_exam(shm);
            }
        }
        semaphore_v(semid, SEM_EXAM); 
    }
    
    printf("TA %d - Nothing left to do.\n", num);
    shmdt(shm);
    exit(0);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        perror("Error: Must pass one argument!");
        return EXIT_FAILURE;
    }
    int num_tas = atoi(argv[1]);
    if (num_tas < 2) {
        perror("Error: Must have at least 2 TAs!");
        return EXIT_FAILURE;
    }

    int shmid = shmget(IPC_PRIVATE, sizeof(shared_memory), 0666 | IPC_CREAT);
    if (shmid == -1) {
        perror("Error: shmget failed!");
        return EXIT_FAILURE;
    }

    shared_memory *shm = (shared_memory *)shmat(shmid, NULL, 0);
    if (shm == (void *)-1) {
        perror("Error: shmat failed!");
        return EXIT_FAILURE;
    }

    int semid = semget(IPC_PRIVATE, 2, 0666 | IPC_CREAT);
    if (semid == -1) {
        perror("Error: semget failed!");
        return EXIT_FAILURE;
    }

    union semun sem_init;
    sem_init.val = 1;

    if (semctl(semid, SEM_RUBRIC, SETVAL, sem_init) == -1) {
        perror("Error: semctl init rubric failed!");
        return EXIT_FAILURE;
    }
    if (semctl(semid, SEM_EXAM, SETVAL, sem_init) == -1) {
        perror("Error: semctl init exam failed!");
        return EXIT_FAILURE;
    }

    load_rubric(shm);
    shm->current_exam = 0;
    load_exam(shm);

    for (int i = 0; i < num_tas; i++) {
        if (fork() == 0) {
            ta_process(i + 1, shmid, semid);
        }
    }

    for (int i = 0; i < num_tas; i++) {
        wait(NULL);
    }

    shmdt(shm);
    shmctl(shmid, IPC_RMID, NULL);
    semctl(semid, 0, IPC_RMID, sem_init);
    
    return EXIT_SUCCESS;
}