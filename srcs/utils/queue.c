#include "queue.h"
#include "../display/display.h"

int is_empty(Queue *q)
{
    return q->front == q->rear;
}

int is_full(Queue *q)
{
    return (q->rear + 1) % QUEUE_SIZE == q->front;
}

void init_queue(Queue *q)
{
    memset(q, 0, sizeof(Queue));
}

void enqueue(Queue *q, int pid, int status)
{
    data_t foo;

    if (is_full(q))
    {
        puts_color("Queue is full\n", RED);
        dequeue(q, &foo);
        // return;
    }
    q->data[q->rear].pid = pid;
    q->data[q->rear].status = status;
    q->rear = (q->rear + 1) % QUEUE_SIZE;
}

int dequeue(Queue *q, data_t *data)
{
    if (is_empty(q))
    {
        return 0;
    }
    data->pid = q->data[q->front].pid;
    data->status = q->data[q->front].status;
    for (int i = 0; i < QUEUE_SIZE - 1; i++)
    {
        q->data[i] = q->data[i + 1];
    }
    q->data[QUEUE_SIZE - 1].pid = 0;
    q->data[QUEUE_SIZE - 1].status = 0;
    q->rear = (q->rear - 1 + QUEUE_SIZE) % QUEUE_SIZE;
    return 1;
}
