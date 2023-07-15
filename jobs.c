#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <sched.h>

#include "main.h"

static int cur_id = 0;
static struct Job *queued_jobs;
static struct Job *finished_jobs;

int get_new_jobid()
{
  return cur_id++;
}

struct Job *init_queued_job(int deadtime, int cpus_per_task)
{
  struct Job *j = (struct Job *)malloc(sizeof(struct Job));
  j->jobid = get_new_jobid();
  j->deadtime = deadtime;
  j->cpus_per_task = cpus_per_task;
  j->status = Queued;
  j->pid = -1;
  CPU_ZERO(&j->occupied_cpus);
  return j;
}

struct Job *find_job(int jobid)
{
  struct Job *cur = queued_jobs;
  while (cur != NULL)
  {
    if (cur->jobid == jobid)
    {
      return cur;
    }
    cur = cur->next;
  }

  cur = finished_jobs;
  while (cur != NULL)
  {
    if (cur->jobid == jobid)
    {
      return cur;
    }
    cur = cur->next;
  }
  return NULL;
}

void add_job(struct Job *j, FILE *log)
{
  fprintf(log, "Adding job %d\n", j->jobid);
  if (queued_jobs == NULL)
  {
    queued_jobs = j;
    return;
  }
  struct Job *cur = queued_jobs;
  while (cur->next != NULL)
  {
    cur = cur->next;
  }
  cur->next = j;
}

void add_job_to_finished(struct Job *j)
{
  j->next = finished_jobs;
  finished_jobs = j;
}

struct Job *delete_job_from_queued(struct Job *j)
{
  struct Job *cur = queued_jobs;
  if (cur == j)
  {
    queued_jobs = j->next;
    return j;
  }
  while (cur->next != j)
  {
    if (cur->next == NULL)
    {
      return j;
    }
    cur = cur->next;
  }
  cur->next = j->next;
  return j;
}

void remove_job(int id)
{
  struct Job *j = find_job(id);
  delete_job_from_queued(j);
  free(j);
}

void remove_all_jobs(FILE *fp)
{
  struct Job *cur = queued_jobs;
  while (cur != NULL)
  {
    fprintf(fp, "Removing job %d\n", cur->jobid);
    struct Job *next = cur->next;
    free(cur);
    cur = next;
  }
  queued_jobs = NULL;
}

void remove_finished_jobs()
{
  struct Job *cur = queued_jobs;
  int flag = 1;
  while (cur != NULL)
  {
    struct Job *next = cur->next;
    if (cur->status == Finished)
    {
      cur->next = finished_jobs;
      finished_jobs = cur->next;
    }
    else if (flag)
    {
      queued_jobs = cur;
    }
    cur = next;
  }
}

struct Job *get_next_job_to_run(int free_cpu, FILE *log)
{
  if (queued_jobs == NULL)
    return NULL;
  if (free_cpu <= 0)
    return NULL;
  struct Job *cur = queued_jobs;

  while (cur != NULL)
  {
    struct Job *next = cur->next;
    if (cur->status == Queued && cur->cpus_per_task <= free_cpu)
    {
      fprintf(log, "Found job %d to run\n", cur->jobid);
      fflush(log);
      return cur;
    }
    cur = next;
  }

  fflush(log);
  return NULL;
}

int kill_job_when_no_conn(struct Job *j)
{
  return kill(j->pid, SIGTERM);
}

void mark_job_as_allocating(struct Job *j)
{
  j->status = Allocating;
}

void mark_job_as_running(struct Job *j)
{
  j->status = Running;
}

void mark_job_as_finished(struct Job *j)
{
  j->status = Finished;
  delete_job_from_queued(j);
  add_job_to_finished(j);
}

void mark_job_as_cancelled(struct Job *j)
{
  j->status = Cancelled;
  delete_job_from_queued(j);
  add_job_to_finished(j);
}