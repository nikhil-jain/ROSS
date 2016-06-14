#include <ross.h>
#include <sys/stat.h>

char g_st_stats_out[128] = {0};
int g_st_stats_enabled = 0;
long g_st_time_interval = 0;
long g_st_current_interval = 0;
tw_clock g_st_real_time_samp = 0;
tw_clock g_st_real_samp_start_cycles = 0;
int g_st_pe_per_file = 1;
int g_st_my_file_id = 0;
tw_stat_list *g_st_stat_head = NULL;
tw_stat_list *g_st_stat_tail = NULL;
MPI_File gvt_file;
MPI_File interval_file;
static tw_statistics last_stats = {0};
static tw_stat last_all_reduce_cnt = 0;
int g_st_disable_out = 0;


static const tw_optdef stats_options[] = {
    TWOPT_GROUP("ROSS Stats"),
    TWOPT_UINT("enable-gvt-stats", g_st_stats_enabled, "Collect data after each GVT; 0 no stats, 1 for stats"), 
    TWOPT_UINT("time-interval", g_st_time_interval, "collect stats for specified sim time interval"), 
    TWOPT_UINT("real-time-samp", g_st_real_time_samp, "real time sampling interval in ms"), 
    TWOPT_CHAR("stats-filename", g_st_stats_out, "prefix for filename(s) for stats output"),
    TWOPT_UINT("pe-per-file", g_st_pe_per_file, "how many PEs to output per file"), 
    TWOPT_UINT("disable-output", g_st_disable_out, "used for perturbation analysis; buffer never dumped to file when 1"), 
    TWOPT_END()
};

const tw_optdef *tw_stats_setup(void)
{
	return stats_options;
}

void st_stats_init()
{
    if (!g_st_disable_out && g_tw_mynode == 0) {
        FILE *file;
        char filename[64];
        sprintf(filename, "read-me.txt");
        file = fopen(filename, "w");
        fprintf(file, "Info for ROSS run.\n\n");
#if HAVE_CTIME
        time_t raw_time;
        time(&raw_time);
        fprintf(file, "Date Created:\t%s\n", ctime(&raw_time));
#endif
        fprintf(file, "## BUILD CONFIGURATION\n\n");
#ifdef ROSS_VERSION
        fprintf(file, "ROSS Version:\t%s\n", ROSS_VERSION);
#endif
        //fprintf(file, "MODEL Version:\t%s\n", model_version);
        fprintf(file, "\n## RUN TIME SETTINGS by GROUP:\n\n");
        tw_opt_settings(file);
        fclose(file);
    }
}

/** write header line to stats output files */
void tw_gvt_stats_file_setup(tw_peid id)
{
    int max_files_directory = 100;
    char directory_path[128];
    char filename[256];
    if (g_st_stats_out[0])
    {
        sprintf(directory_path, "%s-gvt-%d", g_st_stats_out, g_st_my_file_id/max_files_directory);
        mkdir(directory_path, S_IRUSR | S_IWUSR | S_IXUSR);
        sprintf(filename, "%s/%s-%d-gvt.txt", directory_path, g_st_stats_out, g_st_my_file_id);
    }
    else
    {
        sprintf(directory_path, "ross-gvt-%d", g_st_my_file_id/max_files_directory);
        mkdir(directory_path, S_IRUSR | S_IWUSR | S_IXUSR);
        sprintf( filename, "%s/ross-gvt-stats-%d.txt", directory_path, g_st_my_file_id);
    }

    MPI_File_open(stats_comm, filename, MPI_MODE_CREATE | MPI_MODE_WRONLY, MPI_INFO_NULL, &gvt_file);
    
    char buffer[1024];
	sprintf(buffer, "PE,GVT,Total All Reduce Calls," 
        "total events processed,events aborted,events rolled back,event ties detected in PE queues," 
        "efficiency,total remote network events processed," 
        "total rollbacks,primary rollbacks,secondary roll backs,fossil collect attempts," 
        "net events processed,remote sends,remote recvs\n");
    MPI_File_write(gvt_file, buffer, strlen(buffer), MPI_CHAR, MPI_STATUS_IGNORE);
}

/** write header line to stats output files */
void tw_interval_stats_file_setup(tw_peid id)
{
/*    tw_stat forward_events;
    tw_stat reverse_events;
    tw_stat num_gvts;
    tw_stat all_reduce_calls;
    tw_stat events_aborted;
    tw_stat pe_event_ties;

    tw_stat nevents_remote;
    tw_stat nsend_network;
    tw_stat nread_network;

    tw_stat events_rbs;
    tw_stat rb_primary;
    tw_stat rb_secondary;
    tw_stat fc_attempts;
    */
    int max_files_directory = 100;
    char directory_path[128];
    char filename[256];
    if (g_st_stats_out[0])
    {
        sprintf(directory_path, "%s-interval-%d", g_st_stats_out, g_st_my_file_id/max_files_directory);
        mkdir(directory_path, S_IRUSR | S_IWUSR | S_IXUSR);
        sprintf(filename, "%s/%s-%d-interval.txt", directory_path, g_st_stats_out, g_st_my_file_id);
    }
    else
    {
        sprintf(directory_path, "ross-interval-%d", g_st_my_file_id/max_files_directory);
        mkdir(directory_path, S_IRUSR | S_IWUSR | S_IXUSR);
        sprintf( filename, "%s/ross-interval-stats-%d.txt", directory_path, g_st_my_file_id);
    }

    MPI_File_open(stats_comm, filename, MPI_MODE_CREATE | MPI_MODE_WRONLY, MPI_INFO_NULL, &interval_file);
    
    char buffer[1024];
	sprintf(buffer, "PE,interval,forward events,reverse events,number of GVT comps,all reduce calls,"
        "events aborted,event ties detected in PE queues,remote events,network sends,network recvs,"
        "events rolled back,primary rollbacks,secondary roll backs,fossil collect attempts\n");
    MPI_File_write(interval_file, buffer, strlen(buffer), MPI_CHAR, MPI_STATUS_IGNORE);
}

void
tw_gvt_log(FILE * f, tw_pe *me, tw_stime gvt, tw_statistics *s, tw_stat all_reduce_cnt)
{
	// GVT,Forced GVT, Total GVT Computations, Total All Reduce Calls, Average Reduction / GVT
    // total events processed, events aborted, events rolled back, event ties detected in PE queues
    // efficiency, total remote network events processed, percent remote events
    // total rollbacks, primary rollbacks, secondary roll backs, fossil collect attempts
    // net events processed, remote sends, remote recvs
    tw_clock start_cycle_time = tw_clock_read();
    int buf_size = sizeof(tw_stat) * 13 + sizeof(tw_node) + sizeof(tw_stime) + sizeof(double); 
    int index = 0;
    char buffer[buf_size];
    tw_stat tmp;
    double eff;
	/*sprintf(buffer, "%ld,%f,%lld,%lld,%lld,%lld,%lld,%f,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld\n", 
            g_tw_mynode, gvt, all_reduce_cnt-last_all_reduce_cnt, 
            s->s_nevent_processed-last_stats.s_nevent_processed, s->s_nevent_abort-last_stats.s_nevent_abort, 
            s->s_e_rbs-last_stats.s_e_rbs, s->s_pe_event_ties-last_stats.s_pe_event_ties,
            100.0 * (1.0 - ((double) (s->s_e_rbs-last_stats.s_e_rbs)/(double) (s->s_net_events-last_stats.s_net_events))),
            s->s_nsend_net_remote-last_stats.s_nsend_net_remote,
            s->s_rb_total-last_stats.s_rb_total, s->s_rb_primary-last_stats.s_rb_primary,
            s->s_rb_secondary-last_stats.s_rb_secondary, s->s_fc_attempts-last_stats.s_fc_attempts,
            s->s_net_events-last_stats.s_net_events, s->s_nsend_network-last_stats.s_nsend_network, 
            s->s_nread_network-last_stats.s_nread_network);
    */
    memcpy(buffer, &g_tw_mynode, sizeof(tw_node));
    memcpy(buffer, &gvt, sizeof(tw_stime));
    tmp = all_reduce_cnt-last_all_reduce_cnt;
    memcpy(buffer, &tmp, sizeof(tw_stat));

    tmp = s->s_nevent_processed-last_stats.s_nevent_processed;
    memcpy(buffer, &tmp, sizeof(tw_stat));

    tmp = s->s_nevent_abort-last_stats.s_nevent_abort;
    memcpy(buffer, &tmp, sizeof(tw_stat));

    tmp = s->s_e_rbs-last_stats.s_e_rbs;
    memcpy(buffer, &tmp, sizeof(tw_stat));

    tmp = s->s_pe_event_ties-last_stats.s_pe_event_ties;
    memcpy(buffer, &tmp, sizeof(tw_stat));

    eff = 100.0 * (1.0 - ((double) (s->s_e_rbs-last_stats.s_e_rbs)/(double) (s->s_net_events-last_stats.s_net_events)));
    memcpy(buffer, &eff, sizeof(double));

    tmp = s->s_nsend_net_remote-last_stats.s_nsend_net_remote;
    memcpy(buffer, &tmp, sizeof(tw_stat));

    tmp = s->s_rb_total-last_stats.s_rb_total;
    memcpy(buffer, &tmp, sizeof(tw_stat));

    tmp = s->s_rb_primary-last_stats.s_rb_primary;
    memcpy(buffer, &tmp, sizeof(tw_stat));

    tmp = s->s_rb_secondary-last_stats.s_rb_secondary;
    memcpy(buffer, &tmp, sizeof(tw_stat));

    tmp = s->s_fc_attempts-last_stats.s_fc_attempts;
    memcpy(buffer, &tmp, sizeof(tw_stat));

    tmp = s->s_net_events-last_stats.s_net_events;
    memcpy(buffer, &tmp, sizeof(tw_stat));

    tmp = s->s_nsend_network-last_stats.s_nsend_network;
    memcpy(buffer, &tmp, sizeof(tw_stat));

    tmp = s->s_nread_network-last_stats.s_nread_network;
    memcpy(buffer, &tmp, sizeof(tw_stat));

    st_buffer_push(g_st_buffer, &buffer[0], buf_size);
    //stat_comp_cycle_counter += tw_clock_read() - start_cycle_time;
    //start_cycle_time = tw_clock_read();
    //MPI_File_write(gvt_file, buffer, strlen(buffer), MPI_CHAR, MPI_STATUS_IGNORE);
    //stat_write_cycle_counter += tw_clock_read() - start_cycle_time;
    //start_cycle_time = tw_clock_read();
    memcpy(&last_stats, s, sizeof(tw_statistics));
    last_all_reduce_cnt = all_reduce_cnt;
    stat_comp_cycle_counter += tw_clock_read() - start_cycle_time;
}


void get_time_ahead_GVT(tw_pe *me, tw_stime current_rt)
{
    FILE *f;
    f = fopen("test.out", "a");
    tw_kp *kp;
    tw_stime time;
    int i, index = 0;
    int element_size = sizeof(tw_stime);
    //int num_bytes = (g_tw_nkp + 1) * element_size + sizeof(tw_peid);
    //int num_bytes = g_tw_nkp * (sizeof(tw_kpid) + sizeof(tw_peid) + sizeof(tw_stime));
    int num_bytes = g_tw_nkp * (sizeof(tw_kpid) + 2* sizeof(tw_stime));
    char data[num_bytes];
    tw_peid id = me->id;
    //memcpy(&data[index], &id, sizeof(tw_peid));
    //index += sizeof(tw_peid);
    //memcpy(&data[index], &current_rt, element_size);
    //index += element_size;

    fprintf(f,"%f,", current_rt);    
    for(i = 0; i < g_tw_nkp; i++)
    {
        kp = tw_getkp(i);
        //memcpy(&data[index], &id, sizeof(tw_peid));
        //index += sizeof(tw_peid);
        //memcpy(&data[index], &current_rt, element_size);
        memcpy(&data[index], &kp->id, sizeof(tw_kpid));
        index += sizeof(tw_kpid);
        memcpy(&data[index], &current_rt, element_size);
        index += element_size;
        time = kp->last_time - me->GVT;
        memcpy(&data[index], &time, sizeof(tw_stime));
        index += sizeof(tw_stime);

        // TODO remove print after debug
        fprintf(f,"%f", kp->last_time - me->GVT);
        if(i < g_tw_nkp - 1)
           fprintf(f,",");
        else
           fprintf(f,"\n"); 
    }
    st_buffer_push(g_st_buffer, data, num_bytes);
    fclose(f);
}


