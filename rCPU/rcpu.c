#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h> // needed to run server on a new thread
#include <termios.h> // needed for unbuffered_getch()

#include "dwebsvr.h"

// embedded resource files
#include "code.h"
#include "index.h"
#include "jquery-2-1-0-min.h"
#include "smoothie.h"

void* polling_thread(void *args);
void send_response(struct hitArgs *args, char*, char*, http_verb);
void log_filter(log_type, char*, char*, int);
void send_cpu_response(struct hitArgs *args, char*, char*);
void send_temp_response(struct hitArgs *args, char*, char*);
int path_ends_with(char *path, char *match);
void send_file_response(struct hitArgs *args, char*, char*, int);
int get_cpu_count();
void get_cpu_use(int *arr, int len);

int max_cpu;
int *arr;
pthread_t polling_thread_id;

int main(int argc, char **argv)
{
	if (argc < 2 || !strncmp(argv[1], "-h", 2))
	{
		printf("hint: rcpu [port number]\n");
		return 0;
	}
	
    max_cpu = get_cpu_count();
    arr = malloc(max_cpu * sizeof(int));

    if (pthread_create(&polling_thread_id, NULL, polling_thread, NULL) !=0)
    {
        puts("Error: pthread_create could not create polling thread");
        return 0;
    }
    
    // don't read from the console or log anything
    dwebserver(atoi(argv[1]), &send_response, &log_filter);
    
    return 1; // just to stop compiler complaints
}

void* polling_thread(void *args)
{
    while (1)
    {
        get_cpu_use(arr, max_cpu);
        sleep(1);
    }
    return NULL;
}

void log_filter(log_type type, char *s1, char *s2, int socket_fd)
{
    // log to null :-)
}

// decide if we need to send an API response or a file...
void send_response(struct hitArgs *args, char *path, char *request_body, http_verb type)
{
    int path_length=(int)strlen(path);
    if (path_ends_with(path, "cpu.api"))
	{
		return send_cpu_response(args, path, request_body);
	}
    if (path_ends_with(path, "temp.api"))
    {
        return send_temp_response(args, path, request_body);
    }
    if (path_length==0)
	{
        return send_file_response(args, "index.html", request_body, 10);
	}
    send_file_response(args, path, request_body, path_length);
}

// receives a number, returns the current CPU use
void send_cpu_response(struct hitArgs *args, char *path, char *request_body)
{
	char tmp[4];
        
	if (args->form_value_counter==1 && !strncmp(form_name(args, 0), "counter", strlen(form_name(args, 0))))
	{
        STRING *response = new_string(32);
        string_add(response, "[");
        for (int p=0; p<max_cpu; p++)
        {
            sprintf(tmp, "%d", arr[p]);
            string_add(response, tmp);
            if (p < max_cpu-1)
            {
                string_add(response, ", ");
            }
        }
        string_add(response, "]");
        
        int c = atoi(form_value(args, 0));
		if (c > max_cpu) c=0;
        // TODO: use c if needed
		
		ok_200(args, "\nContent-Type: application/json", string_chars(response), path);
        string_free(response);
	}
	else
	{
		forbidden_403(args, "Bad request");
	}
}

// receives a number, returns the current CPU temperature
void send_temp_response(struct hitArgs *args, char *path, char *request_body)
{
#ifdef __APPLE__
    char tmp[13];
    double cpu_temp = rand() % 100;
    STRING *response = new_string(32);
    sprintf(tmp, "%6.3f C", cpu_temp);
    string_add(response, tmp);
    ok_200(args, "\nContent-Type: text/plain", string_chars(response), path);
    string_free(response);
    return;
#else
    char tmp[13];
    double cpu_temp = 0;
    STRING *response = new_string(32);
    
    FILE *temperature = fopen ("/sys/class/thermal/thermal_zone0/temp", "r");
    if (temperature != NULL)
    {
        fscanf(temperature, "%lf", &cpu_temp);
        cpu_temp /= 1000;
        sprintf(tmp, "%6.3f C", cpu_temp);
        fclose (temperature);
    }
    else
    {
        sprintf(tmp, "unknown");
    }
    
    string_add(response, tmp);
    ok_200(args, "\nContent-Type: text/plain", string_chars(response), path);
    string_free(response);
#endif
}

int path_ends_with(char *path, char *match)
{
    int match_len = (int)strlen(match);
    int path_length = (int)strlen(path);
    return (path_length >= match_len && !strncmp(&path[path_length-match_len], match, match_len));
}

void send_file_response(struct hitArgs *args, char *path, char *request_body, int path_length)
{
	long len = 0;
    STRING *response = new_string(1024);
    string_add(response, "HTTP/1.1 200 OK\n");
    string_add(response, "Connection: close\n");
    string_add(response, "Content-Type: ");
    
	if (!strcmp(path, "") || path_ends_with(path, "index.html"))
    {
        string_add(response, "text/html");
        write_header(args->socketfd, string_chars(response), index_html_len);
        write(args->socketfd, index_html, index_html_len);
    }
    if (path_ends_with(path, "code.js"))
    {
        string_add(response, "text/javascript");
        write_header(args->socketfd, string_chars(response), code_js_len);
        write(args->socketfd, code_js, code_js_len);
    }
    if (path_ends_with(path, "smoothie.js"))
    {
        string_add(response, "text/javascript");
        write_header(args->socketfd, string_chars(response), smoothie_js_len);
        write(args->socketfd, smoothie_js, smoothie_js_len);
    }
    if (path_ends_with(path, "jquery-2-1-0-min.js"))
    {
        string_add(response, "text/javascript");
        write_header(args->socketfd, string_chars(response), jquery_2_1_0_min_js_len);
        write(args->socketfd, jquery_2_1_0_min_js, jquery_2_1_0_min_js_len);
    }
	
    if (len==0)
    {
        string_free(response);
        return notfound_404(args, "no such file");
    }
    
    string_free(response);
    
    // allow socket to drain before closing
	sleep(1);
}


// this was adapted from here: http://phoxis.org/2013/09/05/finding-overall-and-per-core-cpu-utilization

#define BUF_MAX 1024

int read_fields (FILE *fp, unsigned long long int *fields)
{
  int retval;
  char buffer[BUF_MAX];

  if (!fgets (buffer, BUF_MAX, fp))
  {
      return -1;
  }

  /* line starts with c and a string. This is to handle cpu, cpu[0-9]+ */
  retval = sscanf (buffer, "c%*s %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
        &fields[0], &fields[1], &fields[2], &fields[3], &fields[4],
        &fields[5], &fields[6], &fields[7], &fields[8], &fields[9]);

  if (retval == 0)
  {
      return -1;
  }

  if (retval < 4) /* At least 4 fields is to be read */
  {
    fprintf (stderr, "Error reading /proc/stat cpu field\n");
    return 0;
  }

  return 1;
}

// TODO: make this simpler, just count the number of lines?
int get_cpu_count()
{
#ifdef __APPLE__
        return 2;
#endif
    
    int count=0;
	unsigned long long int fields[10];
	FILE *fp = fopen ("/proc/stat", "r");
	if (fp == NULL)
	{
		return -1;
	}

	while (read_fields (fp, fields) != -1) count++;
    fclose (fp);
    return count;
}

void get_cpu_use(int *cpu, int len)
{
	unsigned long long int fields[10], total_tick[len], total_tick_old[len], idle[len], idle_old[len], del_total_tick[len], del_idle[len];
	int i, count, cpus = 0;
	double percent_usage;
    
#ifdef __APPLE__
    for (count = 0; count < len; count++)
    {
        cpu[count] = rand() % 100;
    }
    return;
#endif
    
	FILE *fp = fopen ("/proc/stat", "r");
	if (fp == NULL)
	{
		return;
	}

	while (read_fields (fp, fields) != -1)
	{
		for (i=0, total_tick[cpus] = 0; i<10; i++)
		{
			total_tick[cpus] += fields[i];
		}
		idle[cpus] = fields[3]; /* idle ticks index */
		cpus++;
	}

	sleep (1);
	fseek (fp, 0, SEEK_SET);
	fflush (fp);

    for (count = 0; count < len; count++)
    {
      total_tick_old[count] = total_tick[count];
      idle_old[count] = idle[count];
    
      if (!read_fields (fp, fields))
      {
          fclose (fp);
          return;
      }

      for (i=0, total_tick[count] = 0; i<10; i++)
      {
          total_tick[count] += fields[i];
      }
      idle[count] = fields[3];

      del_total_tick[count] = total_tick[count] - total_tick_old[count];
      del_idle[count] = idle[count] - idle_old[count];
      percent_usage = ((del_total_tick[count] - del_idle[count]) / (double) del_total_tick[count]) * 100;
      
      cpu[count] = (int)percent_usage;
    }
    
	fclose (fp);
}
