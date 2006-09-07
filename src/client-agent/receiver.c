/* @(#) $Id$ */

/* Copyright (C) 2005,2006 Daniel B. Cid <dcid@ossec.net>
 * All right reserved.
 *
 * This program is a free software; you can redistribute it
 * and/or modify it under the terms of the GNU General Public
 * License (version 2) as published by the FSF - Free Software
 * Foundation
 */


#include "shared.h"

#include "os_crypto/md5/md5_op.h"
#include "os_net/os_net.h"


#include "agentd.h"



/* receiver_thread: 
 * Receive events from the server.
 */
void *receiver_thread(void *none)
{
    int recv_b;
    
    char file[OS_MAXSTR +1];
    char buffer[OS_MAXSTR +1];
    
    char cleartext[OS_MAXSTR + 1];
    char *tmp_msg;
   
    char file_sum[34];
     
    FILE *fp;


    /* Setting FP to null, before starting */
    fp = NULL;
   
    memset(cleartext, '\0', OS_MAXSTR +1);
    memset(file, '\0', OS_MAXSTR +1);
    memset(file_sum, '\0', 34);
    
    
    while(1)
    {
        
        #ifndef WIN32
        /* locking mutex */
        if(pthread_mutex_lock(&receiver_mutex) != 0)
        {
            merror(MUTEX_ERROR, ARGV0);
            return(NULL);
        }

        if(available_receiver == 0)
        {
            pthread_cond_wait(&receiver_cond, &receiver_mutex);
        }


        /* Setting availables to 0 */
        available_receiver = 0;

        
        /* Unlocking mutex */
        if(pthread_mutex_unlock(&receiver_mutex) != 0)
        {
            merror(MUTEX_ERROR, ARGV0);
            return(NULL);
        }
        
        /* Read until no more messages are available */ 
        while((recv_b = recv(logr->sock, buffer, OS_MAXSTR, MSG_DONTWAIT)) > 0)
        #else
        while((recv_b = recv(logr->sock, buffer, OS_MAXSTR, 0)) > 0)
        #endif
        {
            
            /* Id of zero -- only one key allowed */
            tmp_msg = ReadSecMSG(&keys, buffer, cleartext, 0, recv_b -1);
            if(tmp_msg == NULL)
            {
                merror(MSG_ERROR,ARGV0,logr->rip);
                continue;
            }


            /* Check for commands */
            if(IsValidHeader(tmp_msg))
            {
                /* This is the only thread that modifies it */
                available_server = (int)time(NULL);
                
                
                /* If it is an active response message */
                if(strncmp(tmp_msg, EXECD_HEADER, strlen(EXECD_HEADER)) == 0)
                {
                    #ifndef WIN32
                    tmp_msg+=strlen(EXECD_HEADER);
                    if(logr->execdq >= 0)
                    {
                        if(OS_SendUnix(logr->execdq, tmp_msg, 0) < 0)
                        {
                            merror("%s: Error communicating with execd", 
                            ARGV0);
                        }
                    }

                    #endif    
                    continue;
                } 


                /* Ack from server */
                else if(strcmp(tmp_msg, HC_ACK) == 0)
                {
                    continue;
                }

                /* Close any open file pointer if it was being written to */
                if(fp)
                {
                    fclose(fp);
                    fp = NULL;
                }

                /* File update message */
                if(strncmp(tmp_msg, FILE_UPDATE_HEADER, 
                                    strlen(FILE_UPDATE_HEADER)) == 0)
                {
                    char *validate_file;
                    tmp_msg+=strlen(FILE_UPDATE_HEADER);

                    /* Going to after the file sum */
                    validate_file = strchr(tmp_msg, ' ');
                    if(!validate_file)
                    {
                        continue;
                    }

                    *validate_file = '\0';

                    /* copying the file sum */
                    strncpy(file_sum, tmp_msg, 33);

                    
                    /* Setting tmp_msg to the beginning of the file name */
                    validate_file++;
                    tmp_msg = validate_file;


                    if((validate_file = strchr(tmp_msg, '\n')) != NULL)
                    {
                        *validate_file = '\0';
                    }

                    if((validate_file = strchr(tmp_msg, '/')) != NULL)
                    {
                        *validate_file = '-';
                    }

                    if(tmp_msg[0] == '.')
                        tmp_msg[0] = '-';            

                    
                    snprintf(file, OS_MAXSTR, "%s/.%s", 
                            SHAREDCFG_DIR,
                            tmp_msg);

                    fp = fopen(file, "w");
                    if(!fp)
                    {
                        merror(FOPEN_ERROR, ARGV0, file);
                    }
                }

                else if(strncmp(tmp_msg, FILE_CLOSE_HEADER, 
                                         strlen(FILE_CLOSE_HEADER)) == 0)
                {
                    /* no error */
                    os_md5 currently_md5;

                    if(file[0] == '\0')
                    {
                        /* nada */
                    }

                    else if(OS_MD5_File(file, currently_md5) < 0)
                    {
                        /* Removing file */
                        unlink(file);
                        file[0] = '\0';
                    }
                    else
                    {
                        if(strcmp(currently_md5, file_sum) != 0)
                            unlink(file);
                        else
                        {
                            char *final_file;
                            char _ff[OS_MAXSTR +1];

                            /* Renaming the file to its orignal name */

                            final_file = strchr(file, '.');
                            if(final_file)
                            {
                                final_file++;
                                snprintf(_ff, OS_MAXSTR, "%s/%s",
                                        SHAREDCFG_DIR,
                                        final_file);
                                rename(file, _ff);
                            }
                        }

                        file[0] = '\0';
                    }
                }

                else
                {
                    merror("%s: Unknown message received.", ARGV0);
                }
            }

            else if(fp)
            {
                fprintf(fp, "%s", tmp_msg);
                fflush(fp);
            }

            else
            {
                merror("%s: Unknown message received. No action defined.",
                                                      ARGV0);
            }
        }    
    }

    
    /* Cleaning up */
    if(fp)
    {
        fclose(fp);
        if(file[0] != '\0')
            unlink(file);
    }
        
    return(NULL);

}


/* EOF */
