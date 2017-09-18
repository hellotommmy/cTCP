/******************************************************************************
 * ctcp.c
 * ------
 * Implementation of cTCP done here. This is the only file you need to change.
 * Look at the following files for references and useful functions:
 *   - ctcp.h: Headers for this file.
 *   - ctcp_iinked_list.h: Linked list functions for managing a linked list.
 *   - ctcp_sys.h: Connection-related structs and functions, cTCP segment
 *                 definition.
 *   - ctcp_utils.h: Checksum computation, getting the current time.
 *
 *****************************************************************************/

#include "ctcp.h"
#include "ctcp_linked_list.h"
#include "ctcp_sys.h"
#include "ctcp_utils.h"
#include "ctcp_reno.c"
//#include "ctcp_sys_internal.h"


/**
 * Connection state.
 *
 * Stores per-connection information such as the current sequence number,
 * unacknowledged packets, etc.
 *
 * You should add to this to store other fields you might need.
 */


/**
 * Linked list of connection states. Go through this in ctcp_timer() to
 * resubmit segments and tear down connections.
 */
static ctcp_state_t *state_list;


/* FIXME: Feel free to add as many helper functions as needed. Don't repeat
   code! Helper functions make the code clearer and cleaner. */


ctcp_state_t *ctcp_init(conn_t *conn, ctcp_config_t *cfg) {
    /* Connection could not be established. */
    if (conn == NULL) {
        return NULL;
    }

    /* Established a connection. Create a new state and update the linked list
       of connection states. */
    ctcp_state_t *state = calloc(sizeof(ctcp_state_t), 1);
    if(state == NULL){
        return NULL;
    }
    state->next = state_list;
    state->prev = &state_list;
    if (state_list)
        state_list->prev = &state->next;
    state_list = state;

    /* Set fields. */
    state->conn = conn;
    /* FIXME: Do any other initialization here. */
    state->rwnd = cfg->recv_window;
    state->swnd = cfg->send_window;
    state->timer = cfg->timer;
    state->rto = cfg->rt_timeout;
    state->rcv_buf = (char*)malloc(state->rwnd);
    if(state->rcv_buf == NULL){
        fprintf(stderr, "alloc rcv_buf failed\n");
        goto err;
    }
    state->snd_buf = (char*)malloc(state->swnd);
    if(state->snd_buf == NULL){
        fprintf(stderr, "alloc snd_buf failed\n");
        goto err;
    }
    state->segments = ll_create();
    if(state->segments == NULL){
        fprintf(stderr, "create segments list failed\n");
        goto err;
    }
    state->reorder_list = ll_create();
    if(state->reorder_list == NULL){
        fprintf(stderr, "create reorder list failed\n");
        goto err;
    }
    state->snd_seq = 1;
    state->snd_ack = 1;
    state->rcv_ack = 1;
    state->timestamp = -1;
    state->retry = 0;
    state->fin_send = 0;
    state->fin_recv = 0;
    //initialization of congestion control variables
    state->cwnd = 3;
    state->cwnd_clamp = 65536/(MAX_SEG_DATA_SIZE);
    state->ca_state = CA_Open;
    //state->awnd problem: how to set advertised window at init time?
    state->cwnd_cnt = state->cwnd;
    state->seq_high = 1;
    state->sacked_out = 0;
    state->ssthresh = state->rwnd/(MAX_SEG_DATA_SIZE);
    free(cfg);

    //fprintf(stderr, "ctcp state: rwnd %d, swnd %d, timer %d, rtio %d\n",
            //state->rwnd, state->swnd, state->timer, state->rto);

    return state;
err:
    ctcp_destroy(state);
    return NULL;
}

void ctcp_destroy(ctcp_state_t *state) {
    /* Update linked list. */
    if (state->next)
        state->next->prev = state->prev;

    *state->prev = state->next;
    conn_remove(state->conn);

    /* FIXME: Do any other cleanup here. */
    if(state->rcv_buf)
        free(state->rcv_buf);
    if(state->snd_buf)
        free(state->snd_buf);
    if(state->segments){
        ll_destroy_and_free(state->segments);
    }
    if(state->reorder_list){
        ll_destroy_and_free(state->reorder_list);
    }

    free(state);
    end_client();
}

void ctcp_read(ctcp_state_t *state) {
    /*max_len = max allowed data len*/
    int max_len;
    max_len = state->swnd - (state->snd_seq - state->snd_ack);
    max_len = max_len > MAX_SEG_DATA_SIZE ? MAX_SEG_DATA_SIZE:max_len;
    
    if(max_len > 0 && state->fin_send == 0){
    	char payload[max_len];
    	int data_len;
    	/*read stdin of state->conn*/
    	data_len = conn_input(state->conn,payload,max_len);
    	if(data_len > 0){/* if there is input data*/
    	/*construct and send a data segment*/
    		ctcp_segment_t *data_seg = cons_and_send(state ,data_len ,payload ,ACK);
				if(!data_seg)/*if return value is NULL*/
					return;
    		/*append this segment to state->segments*/
    		if(!append_seg(state , data_seg))
    			return;
    		/*update state->snd_seq*/
    		state->snd_seq += data_len;
    	}
    	else if(data_len == -1){/*else*/
    	//-1 indicates EOF is read
      	fprintf(stderr, "EOF\n");    	
        /*construct and send a FIN segment
          Note that in this segment payload lenghth is 0*/  		
        ctcp_segment_t *fin_seg = cons_and_send(state, 0, NULL, FIN);
        /*            append this segment to state->segments*/
    		if(append_seg(state , fin_seg)!=1)
    			return;        
    		/*update state->snd_seq*/
        state->snd_seq++;
	state->fin_send = 1;	
/*	if(state->fin_send && state->fin_recv &&
    state->snd_seq == state->snd_ack && 
    ll_length(state->reorder_list) == 0){
		ctcp_destroy(state);
		return;
	}*/
    	}

    	if(state->timestamp == -1)
    		state->timestamp = current_time();
    }
}
ctcp_segment_t *cons_and_send(ctcp_state_t * state, int data_len, char * payload, uint32_t flags){
				uint16_t len;
    		ctcp_segment_t *segment;
    		len = (uint16_t)(sizeof(ctcp_segment_t) + data_len);
    		segment = calloc(len, 1);
    		segment->seqno = htonl(state->snd_seq);
    		segment->ackno = htonl(state->rcv_ack);
    		segment->len = htons(len);
    		/*not ack,not FIN,normal*/
    		segment->flags = htonl(flags);
    		segment->window = htons(state->rwnd);
    		segment->cksum = 0;
    		if(data_len)
    			memcpy(segment->data,payload,data_len);
    		segment->cksum = cksum(segment,len);
    		/*sending*/
    		conn_send(state->conn,segment,len);
    	  return segment;
}
int append_seg(ctcp_state_t *state, ctcp_segment_t *seg){
	if(state->segments == NULL)
		state->segments = ll_create();
	ll_add(state->segments , seg);
  return 1;
}
/*
 * @retval: -1 = error or duplicated, 0 success
 */
static int __ordered_insert(linked_list_t *list, ctcp_segment_t *segment){
    ll_node_t *curr, *prev;
    ctcp_segment_t *curr_seg;
    curr = ll_back(list);
    while(curr != NULL){
        prev = curr->prev;
        curr_seg = (ctcp_segment_t*)curr->object;
        if(ntohl(curr_seg->seqno) < ntohl(segment->seqno))
            break;
        else if(ntohl(curr_seg->seqno) == ntohl(segment->seqno))
            return -1;
        curr = prev;
    }
    if(curr == NULL){
        ll_add_front(list, segment);
    }else{
        ll_add_after(list, curr, segment);
    }
    return 0;
}

/*
 * @retval: -1 = empty swnd, means to reset rto timer
 */
static int __move_swnd(linked_list_t *list, uint32_t ackno){
    ll_node_t *curr, *next;
    ctcp_segment_t *curr_seg;
    curr = ll_front(list);
    while(curr != NULL){
        next = curr->next;
        curr_seg = (ctcp_segment_t*)curr->object;
        if(ntohl(curr_seg->seqno) < ackno){
            free(ll_remove(list, curr));
        }else{
            break;
        }
        curr = next;
    }
    if(curr == NULL)
        return -1;
    return 0;
}

static void __send_ack(ctcp_state_t *state){
    //send ack
    ctcp_segment_t ack_seg;
    ack_seg.seqno = htonl(state->snd_seq);
    ack_seg.ackno = htonl(state->rcv_ack);
    ack_seg.len = htons(sizeof(ctcp_segment_t));
    ack_seg.flags = TH_ACK;
    ack_seg.window = htons(state->rwnd);
    ack_seg.cksum = 0;
    ack_seg.cksum = cksum((void*)&ack_seg, sizeof(ctcp_segment_t));
    conn_send(state->conn, &ack_seg, sizeof(ctcp_segment_t));
}

static int __meet_teardown(ctcp_state_t *state){
    //fprintf(stderr, "%d %d %u %u %d\n", state->fin_send,
    //state->fin_recv,
    //state->snd_seq, state->snd_ack,
    //ll_length(state->reorder_list) == 0);
    if(state->fin_send && state->fin_recv &&
    state->snd_seq == state->snd_ack && 
    ll_length(state->reorder_list) == 0){
        return 1;
    }
    return 0;
}




void ctcp_receive(ctcp_state_t *state, ctcp_segment_t *segment, size_t len) {
    /* FIXME */
    //print_hdr_ctcp(segment);
    
    if(cksum(segment, ntohs(segment->len)) == 0xffff){
      	fprintf(fp,"%ld,",current_time());	
        fprintf(fp,"%d\n",state->cwnd);
	    fprintf(stderr,"%ld,",current_time());
	    fprintf(stderr,"%d\n,",state->cwnd);
        if(segment->flags & TH_FIN)
            state->fin_recv = 1;
        //Congestion control related
				state->awnd = ntohs(segment->window);
	//ACK
        if((segment->flags & TH_ACK)&&ntohl(segment->ackno) != 1 ){
            uint32_t ackno = ntohl(segment->ackno);
            //fprintf(stderr,"[ACK received],ackno:%d,state->snd_ack:%d,state:%d\n",ackno,state->snd_ack,state->ca_state);
            if(ackno > state->snd_ack){
            	state->sacked_out = 0;
                if(__move_swnd(state->segments, ackno) < 0)
                    state->timestamp = -1; //clear timer
                else
                    state->timestamp = current_time();
								//Congestion control related
								//state->cwnd_cnt += ackno - state->snd_ack;
								if(state->ca_state == CA_Open){//if CA_Open
									if(state->cwnd > state->ssthresh)
									  ctcp_cong_avoid_ai(state,ackno);
									else
										ctcp_slow_start(state);
								}
								if(state->ca_state == CA_Recovery){
									//fast recover
									if(state->seq_high > ackno){//partial ACK	
										//fprintf(stderr,"partial ACK--------------------------------\n");
										ctcp_fastretrans(state,ackno);
										state->cwnd++;
									}
									else{//full ack
									  //fprintf(stderr,"full ACK-----------------------------------\n");
										state->cwnd = state->ssthresh;
										state->ca_state = 1;
									}
								}
						  //clear retry
						  state->retry = 0;
						  state->snd_ack = ackno;
						}
							#if 1
							#define FP(str...) fprintf(stderr,str)
							#else 
							#define FP(str...) 
							#endif
							else if(ackno == state->snd_ack){
								//duplicate ack
								state->sacked_out++;
								if(state->ca_state == CA_Open){
									if(state->sacked_out>=3){
                    ctcp_recovery(state,state->snd_seq);
										ctcp_fastretrans(state,ackno);
									}
								}
								else if(state->ca_state == CA_Recovery){//receiving agian the duplicate ack
									
									state->timestamp = current_time();
									//FP("time:%ld\n",state->timestamp);
									state->cwnd++;
								}
        }
        }
        //data segment or FIN segment
        if(ntohs(segment->len) > sizeof(ctcp_segment_t) ||
        (segment->flags & TH_FIN)){
        		//fprintf(stderr,"[DATA RECEIVED]\n");
        		//print_hdr_ctcp(segment);
            if(ntohl(segment->seqno) >= state->rcv_ack){
                if(__ordered_insert(state->reorder_list, segment) < 0){//if this segment has been received before(not acked)
                    free(segment);
                    __send_ack(state);
                }
								else{
									state->rwnd -= ntohs(segment->len) - sizeof(ctcp_segment_t);
                	ctcp_output(state);
                }
            }else{//if duplicate data(acked)
                __send_ack(state);
            }
        }
        if(__meet_teardown(state)){
            ctcp_destroy(state);
        }
    }else{
        //corrupt segment
        free(segment);
    }
    if(state){
    	if(state->cwnd > state->cwnd_clamp)
    		state->cwnd = state->cwnd_clamp;
    	state->swnd = state->cwnd*MAX_SEG_DATA_SIZE < state->awnd ? state->cwnd*MAX_SEG_DATA_SIZE : state->awnd;
    }
}

/*
 * @param: buf is used to store inorder data
 *         sz_ret is used to return len of data in buf
 * @retval: ackno
 */
static uint32_t __move_rwnd(linked_list_t *list, uint32_t ackno, char buf[], 
        size_t buf_sz, size_t *sz_ret){
    ll_node_t *curr;
    ctcp_segment_t *curr_seg;
    curr = ll_front(list);
    *sz_ret = 0;
    while(curr != NULL){
        curr_seg = (ctcp_segment_t*)curr->object;
        if(ntohl(curr_seg->seqno) == ackno){
            uint16_t seg_len = ntohs(curr_seg->len) - sizeof(ctcp_segment_t);
            if((*sz_ret + seg_len) <= buf_sz){
                ackno += seg_len;
                memcpy(buf + *sz_ret, curr_seg->data, seg_len);
                *sz_ret += seg_len;
                if(curr_seg->flags & TH_FIN){
                    ackno++;
                }
                free(ll_remove(list, curr));
                curr = ll_front(list);
            }else{
                break;
            }
        }else{
            break;
        }
    }
    return ackno;
}

void ctcp_output(ctcp_state_t *state) {
    /* FIXME */
    size_t buf_sz = conn_bufspace(state->conn);
    uint32_t ackno = __move_rwnd(state->reorder_list, state->rcv_ack,
            state->rcv_buf, buf_sz, &buf_sz);
    if(ackno > state->rcv_ack){
        size_t sent;
        sent = 0;
        //output data
        while(sent < buf_sz){
            size_t ret = conn_output(state->conn, state->rcv_buf + sent, buf_sz - sent);
            if(ret == -1){
                //teardown
                ctcp_destroy(state);
                return;
            }
            sent += ret;
        }
        if(ackno - state->rcv_ack > buf_sz){
            //FIN
            if(conn_output(state->conn, state->rcv_buf, 0) < 0){
                //teardown
                ctcp_destroy(state);
                return;
            }
        }
				state->rwnd += buf_sz;
        state->rcv_ack = ackno;
        __send_ack(state);
    }
    else{//can't move rcv wnd forward--segment out of order
	    //need to send ack
	 	  __send_ack(state);
    }
}

void ctcp_timer() {
    /* FIXME */
    //state_list may be NULL
   long now = current_time();
   if(state_list){
      ctcp_state_t * current_state = state_list;
      ctcp_state_t * next;
      do{
        next = current_state->next;
	if(current_state->timestamp != -1 && now - current_state->timestamp > current_state->rto){
	  if(current_state->retry < 5)
	    ctcp_timeout_retrans(current_state,now);
	  else
	    ctcp_destroy(current_state);
	}
	current_state = next;
      }
      while(current_state && current_state->next != NULL);
   } 
}
