#include <stdio.h>
#include <stdlib.h>

#include "main.h"

static int cur_id = 0;
static struct Job *first_job;

int get_new_jobid()
{
  return cur_id++;
}

void add_job(struct Job *j)
{
  if (first_job == NULL)
  {
    first_job = j;
    return;
  }
  struct Job *cur = first_job;
  while (cur->next != NULL)
  {
    cur = cur->next;
  }
  cur->next = j;
}


void remove_all_jobs(){
  struct Job *cur = first_job;
  while (cur != NULL)
  {
    struct Job *next = cur->next;
    free(cur);
    cur = next;
  }
  first_job = NULL;
}