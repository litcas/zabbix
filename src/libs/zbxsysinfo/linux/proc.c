/*
 * ** ZABBIX
 * ** Copyright (C) 2000-2005 SIA Zabbix
 * **
 * ** This program is free software; you can redistribute it and/or modify
 * ** it under the terms of the GNU General Public License as published by
 * ** the Free Software Foundation; either version 2 of the License, or
 * ** (at your option) any later version.
 * **
 * ** This program is distributed in the hope that it will be useful,
 * ** but WITHOUT ANY WARRANTY; without even the implied warranty of
 * ** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * ** GNU General Public License for more details.
 * **
 * ** You should have received a copy of the GNU General Public License
 * ** along with this program; if not, write to the Free Software
 * ** Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 * **/

#include "config.h"

#include <errno.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef HAVE_PWD_H
#	include <pwd.h>
#endif
#ifdef HAVE_DIRENT_H
	#include <dirent.h>
#endif
#ifdef HAVE_STRINGS_H
	#include <strings.h>
#endif

#include "common.h"
#include "sysinfo.h"

/*
#define FDI(f, m) fprintf(stderr, "DEBUG INFO: " f "\n" , m) // show debug info to stderr
#define SDI(m) FDI("%s", m) // string info
#define IDI(i) FDI("%i", i) // integer info
*/

#define MAX(a, b) ((a)>(b) ? (a) : (b))
#define MIN(a, b) ((a)<(b) ? (a) : (b))
				    
#define DO_SUM 0
#define DO_MAX 1
#define DO_MIN 2
#define DO_AVG 3
				    
int     PROC_MEMORY(const char *cmd, const char *param, double  *value, const char *msg, int mlen_max)
{
#if defined(HAVE_PROC_1_STATUS)

    DIR     *dir;
    struct  dirent *entries;
    struct  stat buf;
    char    filename[MAX_STRING_LEN];
    char    line[MAX_STRING_LEN];

    char    name1[MAX_STRING_LEN];
    char    name2[MAX_STRING_LEN];

    char    procname[MAX_STRING_LEN];
    char    usrname[MAX_STRING_LEN];
    char    mode[MAX_STRING_LEN];

    int     proc_ok = 0;
    int     usr_ok = 0;
    int	    do_task = DO_SUM;


    struct  passwd *usrinfo = NULL;
    long int	lvalue = 0;

    FILE    *f;

    double	memsize = -1;
    int	proccount = 0;

        if(num_param(param) > 3)
        {
                return SYSINFO_RET_FAIL;
        }

        if(get_param(param, 1, procname, MAX_STRING_LEN) != 0)
        {
                return SYSINFO_RET_FAIL;
        }
    
        if(get_param(param, 2, usrname, MAX_STRING_LEN) != 0)
        {
                usrname[0] = 0;
        }
        else
        {
        if(usrname[0] != 0)
        {
                    usrinfo = getpwnam(usrname);
                    if(usrinfo == NULL)
                    {
                /* incorrect user name */
                            return SYSINFO_RET_FAIL;
            }			        
        }
    }

    if(get_param(param, 3, mode, MAX_STRING_LEN) != 0)
    {
        strscpy(mode, "sum");
    }

    if(strcmp(mode,"avg") == 0)
    {
        do_task = DO_AVG;
    }
    else if(strcmp(mode,"max") == 0)
    {
        do_task = DO_MAX;
    }
    else if(strcmp(mode,"min") == 0)
    {
        do_task = DO_MIN;
    }
    else if(strcmp(mode,"sum") == 0)
    {
        do_task = DO_SUM;
    }
    else
    {
        return SYSINFO_RET_FAIL;
    }
    
    dir=opendir("/proc");
    if(NULL == dir)
    {
            return SYSINFO_RET_FAIL;
    }

    while((entries=readdir(dir))!=NULL)
    {
        proc_ok = 0;
        usr_ok = 0;
    
        strscpy(filename,"/proc/");	
        strncat(filename,entries->d_name,MAX_STRING_LEN);
        strncat(filename,"/status",MAX_STRING_LEN);
    
    /* Self is a symbolic link. It leads to incorrect results for proc_cnt[zabbix_agentd] */
    /* Better approach: check if /proc/x/ is symbolic link */
        if(strncmp(entries->d_name,"self",MAX_STRING_LEN) == 0)
        {
            continue;
        }
    
        if(stat(filename,&buf)==0)
        {
            f=fopen(filename,"r");
            if(f==NULL)
            {
                continue;
            }
    
            if(procname[0] != 0)
            {
                fgets(line,MAX_STRING_LEN,f);
                if(sscanf(line,"%s\t%s\n",name1,name2)==2)
                            {
                                    if(strcmp(name1,"Name:") == 0)
                                    {
                                            if(strcmp(procname,name2)==0)
                                            {
                                                    proc_ok = 1;
                                            }
                                    }
                            }
            
                if(proc_ok == 0) 
                {
                    fclose(f);
                    continue;
                }
            }
            else
            {
                proc_ok = 1;
            }
            
            if(usrinfo != NULL)
            {
                while(fgets(line, MAX_STRING_LEN, f) != NULL)
                {	
                
                    if(sscanf(line, "%s\t%li\n", name1, &lvalue) != 2)
                    {
                        continue;
                    }
                    
                    if(strcmp(name1,"Uid:") != 0)
                    {
                        continue;
                    }
                    
                    if(usrinfo->pw_uid == (uid_t)(lvalue))
                    {
                        usr_ok = 1;
                        break;
                    }
                }
            }
            else
            {
                usr_ok = 1;
            }
            
            if(proc_ok && usr_ok)
            {
                while(fgets(line, MAX_STRING_LEN, f) != NULL)
                {	
                
                    if(sscanf(line, "%s\t%li %s\n", name1, &lvalue, name2) != 3)
                    {
                        continue;
                    }
                    
                    if(strcmp(name1,"VmSize:") != 0)
                    {
                        continue;
                    }
                    
                    proccount++;
                    
                    if(strcasecmp(name2, "kB") == 0)
                    {
                        lvalue <<= 10;
                    }
                    else if(strcasecmp(name2, "mB") == 0)
                    {
                        lvalue <<= 20;
                    }
                    else if(strcasecmp(name2, "GB") == 0)
                    {
                        lvalue <<= 30;
                    }
                    else if(strcasecmp(name2, "TB") == 0)
                    {
                        lvalue <<= 40;
                    }
                    
                    if(memsize < 0)
                    {
                        memsize = (double) lvalue;
                    }
                    else
                    {
                        if(do_task == DO_MAX)
                        {
                            memsize = MAX(memsize, (double) lvalue);
                        }
                        else if(do_task == DO_MIN)
                        {
                            memsize = MIN(memsize, (double) lvalue);
                        }
                        else
                        {
                            memsize +=  (double) lvalue;
                        }
                    }
                    
                    break;
                }
            }
            
                    
            fclose(f);
        }
    }
    closedir(dir);
    
    if(memsize < 0)
    {
        /* incorrect process name */
        memsize = 0;
    }
    
    if(do_task == DO_AVG)
    {
        memsize /= (double)proccount;
    }
    
    *value = memsize;
    return SYSINFO_RET_OK;
#else
	return	SYSINFO_RET_FAIL;
#endif
}

int	    PROC_NUM(const char *cmd, const char *param, double *value, const char *msg, int mlen_max)
{
#if defined(HAVE_PROC_1_STATUS)

    DIR     *dir;
    struct  dirent *entries;
    struct  stat buf;
    char    filename[MAX_STRING_LEN];
    char    line[MAX_STRING_LEN];

    char    name1[MAX_STRING_LEN];
    char    name2[MAX_STRING_LEN];

    char    procname[MAX_STRING_LEN];
    char    usrname[MAX_STRING_LEN];
    char    procstat[MAX_STRING_LEN];

    int     proc_ok = 0;
    int     usr_ok = 0;
    int     stat_ok = 0;

    struct  passwd *usrinfo = NULL;
    long int	lvalue = 0;

    FILE    *f;

        int	proccount = 0;
    
        if(num_param(param) > 3)
        {
                return SYSINFO_RET_FAIL;
        }
    
        if(get_param(param, 1, procname, MAX_STRING_LEN) != 0)
        {
                return SYSINFO_RET_FAIL;
        }
    
        if(get_param(param, 2, usrname, MAX_STRING_LEN) != 0)
        {
                usrname[0] = 0;
        }
        else
        {
            if(usrname[0] != 0)
            {
                usrinfo = getpwnam(usrname);
                if(usrinfo == NULL)
                {
                    /* incorrect user name */
                    return SYSINFO_RET_FAIL;
                }			        
            }
        }
    
        if(get_param(param, 3, procstat, MAX_STRING_LEN) != 0)
        {
            strscpy(procstat,"all");
        }
    
        if(strcmp(procstat,"run") == 0)
        {
            strscpy(procstat,"R");	
        }
        else if(strcmp(procstat,"sleep") == 0)
        {
            strscpy(procstat,"S");	
        }
        else if(strcmp(procstat,"zomb") == 0)
        {
            strscpy(procstat,"Z");	
        }
        else if(strcmp(procstat,"all") == 0)
        {
            procstat[0] = 0;
        }
        else
        {
            return SYSINFO_RET_FAIL;
        }
		
        dir=opendir("/proc");
        if(NULL == dir)
        {
            return SYSINFO_RET_FAIL;
        }

        while((entries=readdir(dir))!=NULL)
        {
            proc_ok = 0;
            stat_ok = 0;
            usr_ok = 0;
    
/* Self is a symbolic link. It leads to incorrect results for proc_cnt[zabbix_agentd] */
/* Better approach: check if /proc/x/ is symbolic link */
            if(strncmp(entries->d_name,"self",MAX_STRING_LEN) == 0)
            {
                continue;
            }

            strscpy(filename,"/proc/");	
            strncat(filename,entries->d_name,MAX_STRING_LEN);
            strncat(filename,"/status",MAX_STRING_LEN);

            if(stat(filename,&buf)==0)
            {
                f=fopen(filename,"r");
                if(f==NULL)
                {
                    continue;
                }
    
                if(procname[0] != 0)
                {
                    fgets(line,MAX_STRING_LEN,f);
                    if(sscanf(line,"%s\t%s\n",name1,name2)==2)
                    {
                        if(strcmp(name1,"Name:") == 0)
                        {
                            if(strcmp(procname,name2)==0)
                            {
                                proc_ok = 1;
                            }
                        }
                    }
                
                    if(proc_ok == 0) 
                    {
                        fclose(f);
                        continue;
                    }
                }
                else
                {
                    proc_ok = 1;
                }

                if(procstat[0] != 0)
                {
                    while(fgets(line, MAX_STRING_LEN, f) != NULL)
                    {	
                    
                        if(sscanf(line, "%s\t%s\n", name1, name2) != 2)
                        {
                            continue;
                        }
                        
                        if(strcmp(name1,"State:") != 0)
                        {
                            continue;
                        }
                        
                        if(strcmp(name2, procstat))
                        {
                            stat_ok = 1;
                            break;
                        }
                    }
                }
                else
                {
                    stat_ok = 1;
                }
                
                if(usrinfo != NULL)
                {
                    while(fgets(line, MAX_STRING_LEN, f) != NULL)
                    {	
                    
                        if(sscanf(line, "%s\t%li\n", name1, &lvalue) != 2)
                        {
                            continue;
                        }
                        
                        if(strcmp(name1,"Uid:") != 0)
                        {
                            continue;
                        }
                        
                        if(usrinfo->pw_uid == (uid_t)(lvalue))
                        {
                            usr_ok = 1;
                            break;
                        }
                    }
                }
                else
                {
                    usr_ok = 1;
                }
                
                if(proc_ok && stat_ok && usr_ok)
                {
                    proccount++;
                }
                
                fclose(f);
            }
    }
    closedir(dir);

    *value = (double) proccount;
    return SYSINFO_RET_OK;
#else
    return	SYSINFO_RET_FAIL;
#endif
}
