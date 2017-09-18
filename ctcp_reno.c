#include "ctcp.h"
#include "ctcp_linked_list.h"
#include "ctcp_sys.h"
#include "ctcp_utils.h"
void ctcp_slow_start(ctcp_state_t *state);
void ctcp_cong_avoid_ai(ctcp_state_t *state,uint32_t w);
void ctcp_fastretrans(ctcp_state_t *state,uint32_t ackno);
void ctcp_recovery(ctcp_state_t *state,uint32_t ackno);
void ctcp_timeout_retrans(ctcp_state_t *state,long now);
extern FILE* fp;
void ctcp_timeout_retrans(ctcp_state_t *current_state,long now){
  /*optimistic, send only the first unacked segment*/
  linked_list_t *seg_list = current_state->segments;
  ll_node_t * resend_node = ll_front(seg_list);
	ctcp_segment_t * resend_seg = resend_node->object;
	uint16_t len_seg = ntohs(resend_seg->len);
	conn_send(current_state->conn,resend_seg,len_seg);
	/*may assume len_seg == send_res?I am tired of checking*/	  
	current_state->timestamp = now;
	current_state->retry++;	 
//	fprintf(stderr,"%ld,",current_time());
//	fprintf(stderr,"[Timeout Retransmission]%d retries of packet %d\t",current_state->retry,ntohl(resend_seg->seqno));  
	//fprintf(stderr,"ssthresh:%d\t",current_state->ssthresh);
//	fprintf(stderr,"%d\n",current_state->cwnd);
	//fprintf(stderr,"awnd:%d\t",current_state->awnd);
	//fprintf(stderr,"swnd:%d\n",current_state->swnd);
	current_state->ca_state = CA_Open;
	current_state->ssthresh = 2 > current_state->cwnd/2 ? 2 : current_state->cwnd/2;
	current_state->cwnd = 1;  
}

void ctcp_recovery(ctcp_state_t *state,uint32_t ackno){
	//fprintf(stderr,"[TIME] %ld\t",current_time());

	
//	fprintf(stderr,"[Fast Recovery]");
	//fprintf(stderr,"ssthresh:%d\t",state->ssthresh);
	//fprintf(stderr,"cwnd:%d\t",state->cwnd);
	//fprintf(stderr,"%d\n",state->cwnd);
	//fprintf(stderr,"awnd:%d\t",state->awnd);
	//fprintf(stderr,"swnd:%d\t",state->swnd);
	state->ca_state = CA_Recovery;//4
	state->ssthresh = 2 > state->cwnd/2 ? 2:state->cwnd/2;//max in flight
	state->cwnd = state->ssthresh+3;
	state->seq_high = ackno;
	//fprintf(stderr,"now cwnd is:%d\t",state->cwnd);
	//fprintf(stderr,"now in CA_Recovery state(:%d)\t",state->ca_state);
	//fprintf(stderr,"the next data seqno to send:%d\n",state->seq_high);
}
void ctcp_slow_start(ctcp_state_t *state){
	//fprintf(stderr,"[TIME] %ld\t",current_time());
//	fprintf(stderr,"[Slow Start]");
	//fprintf(stderr,"ssthresh:%d\t",state->ssthresh);
	//fprintf(stderr,"cwnd:%d,will be incremented by 1\t",state->cwnd);
	//fprintf(stderr,"awnd:%d\t",state->awnd);
	//fprintf(stderr,"swnd:%d\n",state->swnd);
	state->cwnd++;
}
void ctcp_fastretrans(ctcp_state_t * current_state, uint32_t ackno){		
  //fprintf(stderr,"[TIME] %ld\t",current_time());
//	fprintf(stderr,"[Fast Retransmission]");
	/*fprintf(stderr,"ssthresh:%d\t",current_state->ssthresh);
	fprintf(stderr,"cwnd:%d\t",current_state->cwnd);
	fprintf(stderr,"awnd:%d\t",current_state->awnd);
	fprintf(stderr,"swnd:%d\t",current_state->swnd);
	fprintf(stderr,"resending %d\n",ackno);*/
	

  linked_list_t *seg_list = current_state->segments;
  ll_node_t * resend_node = ll_front(seg_list);
  ctcp_segment_t * resend_seg = resend_node->object;
  while(resend_node!=NULL){
  	resend_seg = resend_node->object;
  	if(ntohl(resend_seg->seqno) == ackno)
  		break;
  	resend_node = resend_node->next;
  }
  uint16_t len_seg = ntohs(resend_seg->len);
  conn_send(current_state->conn,resend_seg,len_seg);
//  fprintf(stderr,"sent info:\n");
//  print_hdr_ctcp(resend_seg);
  current_state->timestamp = current_time();
  current_state->sacked_out = 0;
}

void ctcp_cong_avoid_ai(ctcp_state_t *state,uint32_t w){
	//fprintf(stderr,"[TIME] %ld\t",current_time());
	//fprintf(stderr,"[Congestion Avoidance]");
/*	fprintf(stderr,"ssthresh:%d\t",state->ssthresh);
	fprintf(stderr,"cwnd:%d\t",state->cwnd);
	fprintf(stderr,"awnd:%d\t",state->awnd);
	fprintf(stderr,"swnd:%d\t",state->swnd);
	fprintf(stderr,"cwnd_cnt:%d\n",state->cwnd_cnt); */

  state->cwnd_cnt++;
	if(state->cwnd_cnt >= state->cwnd){
	  state->cwnd_cnt = 0;
	  state->cwnd++;
	  //fprintf(stderr,"cwnd+1 to %d\n",state->cwnd);
	}
}

