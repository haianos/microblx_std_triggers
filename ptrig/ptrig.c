#include "ptrig.h"

static inline void tsnorm(struct timespec *ts)
{
        if(ts->tv_nsec >= NSEC_PER_SEC) {
                ts->tv_sec+=ts->tv_nsec / NSEC_PER_SEC;
                ts->tv_nsec=ts->tv_nsec % NSEC_PER_SEC;
        }
}

/* trigger the configured blocks */
static int trigger_steps(struct ptrig_info *inf)
{
        int i, steps, ret=-1;
        struct ubx_timespec ts_start, ts_end, ts_dur;

        ubx_clock_mono_gettime(&ts_start);

        for(i=0; i<inf->trig_list_len; i++) {
                for(steps=0; steps<inf->trig_list[i].num_steps; steps++) {
                        if(ubx_cblock_step(inf->trig_list[i].b)!=0)
                                goto out;
                }
        }

        /* compute tstats */
        ubx_clock_mono_gettime(&ts_end);
        ubx_ts_sub(&ts_end, &ts_start, &ts_dur);

        if(ubx_ts_cmp(&ts_dur, &inf->tstats.min) == -1)
                inf->tstats.min=ts_dur;

        if(ubx_ts_cmp(&ts_dur, &inf->tstats.max) == 1)
                inf->tstats.max=ts_dur;

        ubx_ts_add(&inf->tstats.total, &ts_dur, &inf->tstats.total);
        inf->tstats.cnt++;
//         write_tstat(inf->p_tstats, &inf->tstats);

        ret=0;
 out:
        return ret;
}

/* thread entry */
static void* thread_startup(void *arg)
{
        int ret = -1;
        ubx_block_t *b;
        struct ptrig_info *inf;
        struct timespec ts;

        b = (ubx_block_t*) arg;
        inf = (struct ptrig_info*) b->private_data;

        while(1) {
                pthread_mutex_lock(&inf->mutex);

                while(inf->state != BLOCK_STATE_ACTIVE) {
                        pthread_cond_wait(&inf->active_cond, &inf->mutex);
                }
                pthread_mutex_unlock(&inf->mutex);

                if((ret=clock_gettime(CLOCK_MONOTONIC, &ts))) {
                        ERR2(ret, "clock_gettime failed");
                        goto out;
                }

                trigger_steps(inf);

                ts.tv_sec += inf->period.sec;
                ts.tv_nsec += inf->period.usec*NSEC_PER_USEC;
                tsnorm(&ts);

                if((ret=clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL))) {
                        ERR2(ret, "clock_nanosleep failed");
                        goto out;
                }
        }

 out:
        /* block on cond var that signals block is running */
        pthread_exit(NULL);
}

static void ptrig_handle_config(ubx_block_t *b)
{
        int ret;
        unsigned int tmplen, schedpol;
        char *schedpol_str;
        size_t stacksize;
        struct sched_param sched_param; /* prio */
        struct ptrig_info *inf=(struct ptrig_info*) b->private_data;

        /* period */
        inf->period = *((struct ptrig_period*) ubx_config_get_data_ptr(b, "period", &tmplen));

        /* stacksize */
        stacksize = *((int*) ubx_config_get_data_ptr(b, "stacksize", &tmplen));
        if(stacksize != 0) {
                if(stacksize<PTHREAD_STACK_MIN) {
                        ERR("%s: stacksize less than %d", b->name, PTHREAD_STACK_MIN);
                } else {
                        if(pthread_attr_setstacksize(&inf->attr, stacksize))
                                ERR2(errno, "pthread_attr_setstacksize failed");
                }
        }

        /* schedpolicy */
        schedpol_str = (char*) ubx_config_get_data_ptr(b, "sched_policy", &tmplen);
        schedpol_str = (strncmp(schedpol_str, "", tmplen) == 0) ? "SCHED_OTHER" : schedpol_str;

        if (strncmp(schedpol_str, "SCHED_OTHER", tmplen) == 0) {
                schedpol=SCHED_OTHER;
        } else if (strncmp(schedpol_str, "SCHED_FIFO", tmplen) == 0) {
                schedpol = SCHED_FIFO;
        } else if (strncmp(schedpol_str, "SCHED_RR", tmplen) == 0) {
                schedpol = SCHED_RR;
        } else {
                ERR("%s: sched_policy config: illegal value %s", b->name, schedpol_str);
                schedpol=SCHED_OTHER;
        }

        if((schedpol==SCHED_FIFO || schedpol==SCHED_RR) && sched_param.sched_priority > 0) {
                ERR("%s sched_priority is %d with %s policy", b->name, sched_param.sched_priority, schedpol_str);
        }

        if(pthread_attr_setschedpolicy(&inf->attr, schedpol)) {
                ERR("pthread_attr_setschedpolicy failed");
        }

        /* see PTHREAD_ATTR_SETSCHEDPOLICY(3) */
        if((ret=pthread_attr_setinheritsched(&inf->attr, PTHREAD_EXPLICIT_SCHED))!=0)
                ERR2(ret, "failed to set PTHREAD_EXPLICIT_SCHED.");

        /* priority */
        sched_param.sched_priority = *((int*) ubx_config_get_data_ptr(b, "sched_priority", &tmplen));
        if(pthread_attr_setschedparam(&inf->attr, &sched_param))
                ERR("failed to set sched_policy.sched_priority to %d", sched_param.sched_priority);

        DBG("%s config: period=%lus:%luus, policy=%s, prio=%d, stacksize=%lu (0=default size)",
            b->name, inf->period.sec, inf->period.usec, schedpol_str, sched_param.sched_priority, (unsigned long) stacksize);
}

/* init */
int ptrig_init(ubx_block_t *b)
{
        unsigned int tmplen;
        int ret = EOUTOFMEM;
        const char* threadname;
        struct ptrig_info* inf;

        if((b->private_data=calloc(1, sizeof(struct ptrig_info)))==NULL) {
                ERR("failed to alloc");
                goto out;
        }

        inf=(struct ptrig_info*) b->private_data;

        inf->state=BLOCK_STATE_INACTIVE;

        pthread_cond_init(&inf->active_cond, NULL);
        pthread_mutex_init(&inf->mutex, NULL);
        pthread_attr_init(&inf->attr);
        pthread_attr_setdetachstate(&inf->attr, PTHREAD_CREATE_JOINABLE);

        ptrig_handle_config(b);

        if((ret=pthread_create(&inf->tid, &inf->attr, thread_startup, b))!=0) {
                ERR2(ret, "pthread_create failed");
                goto out_err;
        }

#ifdef CONFIG_PTHREAD_SETNAME
        /* setup thread name */
        threadname = (char*) ubx_config_get_data_ptr(b, "thread_name", &tmplen);
        threadname = (strncmp(threadname, "", tmplen)==0) ? b->name : threadname;

        if(pthread_setname_np(inf->tid, threadname))
                ERR("failed to set thread_name to %s", threadname);
#endif

        inf->tstats.min.sec=999999999;
        inf->tstats.min.nsec=999999999;

        inf->tstats.max.sec=0;
        inf->tstats.max.nsec=0;

        inf->tstats.total.sec=0;
        inf->tstats.total.nsec=0;

        /* OK */
        ret=0;
        goto out;

 out_err:
        free(b->private_data);
 out:
        return ret;
}

/* start */
int ptrig_start(ubx_block_t *b)
{
        DBG(" ");

        struct ptrig_info *inf;
        ubx_data_t* trig_list_data;

        inf = (struct ptrig_info*) b->private_data;

        trig_list_data = ubx_config_get_data(b, "trig_blocks");

        /* make a copy? */
        inf->trig_list = trig_list_data->data;
        inf->trig_list_len = trig_list_data->len;

        pthread_mutex_lock(&inf->mutex);
        inf->state=BLOCK_STATE_ACTIVE;
        pthread_cond_signal(&inf->active_cond);
        pthread_mutex_unlock(&inf->mutex);

        assert(inf->p_tstats = ubx_port_get(b, "tstats"));
        return 0;
}

/* stop */
void ptrig_stop(ubx_block_t *b)
{
        DBG(" ");
        struct ptrig_info *inf;
        inf = (struct ptrig_info*) b->private_data;

        pthread_mutex_lock(&inf->mutex);
        inf->state=BLOCK_STATE_INACTIVE;
        pthread_mutex_unlock(&inf->mutex);
}

/* cleanup */
void ptrig_cleanup(ubx_block_t *b)
{
        int ret;
        struct ptrig_info* inf;
        inf=(struct ptrig_info*) b->private_data;

        inf->state=BLOCK_STATE_PREINIT;

        if((ret=pthread_cancel(inf->tid))!=0)
                ERR2(ret, "pthread_cancel failed");

        /* join */
        if((ret=pthread_join(inf->tid, NULL))!=0)
                ERR2(ret, "pthread_join failed");

        pthread_attr_destroy(&inf->attr);
        free(b->private_data);
}


