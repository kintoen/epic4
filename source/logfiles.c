/* $EPIC: logfiles.c,v 1.23 2004/10/25 15:08:02 jnelson Exp $ */
/*
 * logfiles.c - General purpose log files
 *
 * Copyright � 2002 EPIC Software Labs
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notices, the above paragraph (the one permitting redistribution),
 *    this list of conditions and the following disclaimer in the 
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the author(s) may not be used to endorse or promote 
 *    products derived from this software without specific prior written
 *    permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR 
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES 
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT, 
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED 
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, 
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY 
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF 
 * SUCH DAMAGE.
 */
#include "irc.h"
#include "log.h"
#include "vars.h"
#include "output.h"
#include "ircaux.h"
#include "alias.h"
#include "list.h"
#include "server.h"
#include "window.h"
#include "functions.h"

#define MAX_TARGETS 32

#define LOG_TARGETS 0
#define LOG_WINDOWS 1
#define LOG_SERVERS 2

static const char *onoff[] = { "OFF", "ON" };
static const char *logtype[] = { "TARGETS", "WINDOWS", "SERVERS" };

struct Logfile {
	struct Logfile *next;
	int	refnum;
	char *	name;
	char *	filename;
	FILE *	log;
	int	servref;

	int	type;
	WNickList *targets;
	int	refnums[MAX_TARGETS];
	int	level;

	char *	rewrite;
	int	mangler;
	char *	mangle_desc;
	int	active;

	time_t	activity;
};

typedef struct Logfile Logfile;
Logfile *logfiles = NULL;
int	logref = 0;

static Logfile *	new_logfile (void)
{
	Logfile *log;
	int	i;

	log = (Logfile *)new_malloc(sizeof(Logfile));
	log->next = logfiles;
	log->refnum = ++logref;
	log->name = malloc_sprintf(NULL, "%d", log->refnum);
	log->filename = NULL;
	log->log = NULL;
	log->servref = from_server;
	log->type = LOG_TARGETS;
	log->targets = NULL;
	for (i = 0; i < MAX_TARGETS; i++)
		log->refnums[i] = -1;
	log->level = LOG_ALL;
	log->rewrite = NULL;
	log->mangler = 0;
	log->mangle_desc = NULL;
	log->active = 0;
	time(&log->activity);

	logfiles = log;
	return log;
}

static void	delete_logfile (Logfile *log)
{
	Logfile *prev;
	WNickList *next;

	if (log == logfiles)
		logfiles = log->next;
	else
	{
		for (prev = logfiles; prev; prev = prev->next)
			if (prev->next == log)
				break;
		prev->next = log->next;
	}

	new_free(&log->name);
	if (log->active)
		do_log(0, log->filename, &log->log);
	new_free(&log->filename);

	while (log->targets)
	{
		next = log->targets->next;
		new_free(&log->targets->nick);
		new_free((char **)&log->targets);
		log->targets = next;
	}

	new_free(&log->rewrite);
	new_free(&log->mangle_desc);
}

static Logfile *	get_log_by_desc (const char *desc)
{
	Logfile *log;

	if (is_number(desc))
	{
		int	number = atol(desc);

		for (log = logfiles; log; log = log->next)
			if (log->refnum == number)
				return log;
	}
	else
	{
		for (log = logfiles; log; log = log->next)
			if (!my_stricmp(log->name, desc))
				return log;
	}

	return NULL;
}

static int	is_logfile_name_unique (const char *desc)
{
	Logfile *log;

	for (log = logfiles; log; log = log->next)
		if (!my_stricmp(log->name, desc))
			return 0;
	return 1;
}

static void	clean_log_targets (Logfile *log)
{
	int	i;
	WNickList *next;

	for (i = 0; i < MAX_TARGETS; i++)
		log->refnums[i] = -1;

	while (log->targets)
	{
		next = log->targets->next;
		new_free(&log->targets->nick);
		new_free((char **)&log->targets);
		log->targets = next;
	}
}

static char *logfile_get_targets (Logfile *log)
{
	WNickList *tmp;
	char *nicks = NULL;
	int	i;

	if (log->type == LOG_TARGETS)
	{
		for (tmp = log->targets; tmp; tmp = tmp->next)
			malloc_strcat_wordlist(&nicks, ",", tmp->nick);
	}
	else if (log->type == LOG_SERVERS || log->type == LOG_WINDOWS)
	{
		for (i = 0; i < MAX_TARGETS; i++)
			if (log->refnums[i] != -1)
				malloc_strcat_wordlist(&nicks, ",", ltoa(log->refnums[i]));
	}

	return nicks;
}

/************************************************************************/
typedef Logfile *(*logfile_func) (Logfile *, char **);

static Logfile *logfile_activity (Logfile *log, char **args);
static Logfile *logfile_add (Logfile *log, char **args);
static Logfile *logfile_describe (Logfile *log, char **args);
static Logfile *logfile_filename (Logfile *log, char **args);
static Logfile *logfile_kill (Logfile *log, char **args);
static Logfile *logfile_list (Logfile *log, char **args);
static Logfile *logfile_mangle (Logfile *log, char **args);
static Logfile *logfile_name (Logfile *log, char **args);
static Logfile *logfile_new (Logfile *log, char **args);
static Logfile *logfile_off (Logfile *log, char **args);
static Logfile *logfile_on (Logfile *log, char **args);
static Logfile *logfile_refnum (Logfile *log, char **args);
static Logfile *logfile_remove (Logfile *log, char **args);
static Logfile *logfile_rewrite (Logfile *log, char **args);
static Logfile *logfile_type (Logfile *log, char **args);

static Logfile *	logfile_activity (Logfile *log, char **args)
{
	if (!log)
	{
		say("ACTIVITY: You need to specify a logfile first");
		return NULL;
	}

	time(&log->activity);
	return log;
}

static Logfile *	logfile_add (Logfile *log, char **args)
{
        char            *ptr;
        WNickList       *new_w;
        char            *arg = next_arg(*args, args);
	int		i;

	if (!log)
	{
		say("ADD: You need to specify a logfile first");
		return NULL;
	}

        if (!arg)
                say("ADD: Add nicknames/channels to be logged to this file");

        else while (arg)
        {
                if ((ptr = strchr(arg, ',')))
                        *ptr++ = 0;

		if (log->type == LOG_TARGETS)
		{
                    if (!find_in_list((List **)&log->targets, arg, !USE_WILDCARDS))
                    {
                        say("Added %s to log name list", arg);
                        new_w = (WNickList *)new_malloc(sizeof(WNickList));
                        new_w->nick = malloc_strdup(arg);
                        add_to_list((List **)&(log->targets), (List *)new_w);
                    }
                    else
                        say("%s already on log name list", arg);
		}
		else if (log->type == LOG_SERVERS || log->type == LOG_WINDOWS)
		{
		    int refnum = my_atol(arg);
		    for (i = 0; i < MAX_TARGETS; i++)
		    {
			if (log->refnums[i] == refnum)
			{
				say("%s already on log refnum list", arg);
				break;
			}
		    }
		    for (i = 0; i < MAX_TARGETS; i++)
		    {
			if (log->refnums[i] == -1)
			{
				say("Added %d to log name list", refnum);
				log->refnums[i] = refnum;
				break;
			}
		    }
		    if (i >= MAX_TARGETS)
			say("Could not add %d to log name list!", refnum);
		}
                arg = ptr;
        }

        return log;
}

static Logfile *	logfile_describe (Logfile *log, char **args)
{
	char *targets = NULL;

	if (!log)
	{
		say("DESCRIBE: You need to specify a logfile first");
		return NULL;
	}

	targets = logfile_get_targets(log);

	say(" Logfile refnum %d is %s", log->refnum, onoff[log->active]);
	say("\t Logical name: %s", log->name);
	say("\t     Filename: %s", log->filename ? log->filename : "<NONE>");
	say("\t         Type: %s", logtype[log->type]);
	say("\t       Server: %d", log->servref);
	say("\tTarget/Refnum: %s", targets ? targets : "<NONE>");
	say("\t        Level: %s", bits_to_lastlog_level(log->level));
	say("\t Rewrite Rule: %s", log->rewrite ? log->rewrite : "<NONE>");
	say("\t Mangle rules: %s", log->mangle_desc ? log->mangle_desc : "<NONE>");

	new_free(&targets);
	return log;
}

static Logfile *	logfile_filename (Logfile *log, char **args)
{
	char *	arg = next_arg(*args, args);

	if (!log)
	{
		say("FILENAME: You need to specify a logfile first");
		return NULL;
	}

	if (!arg)
	{
		if (log->filename)
			say("Log %s is attached to %s", log->name, log->filename);
		else
			say("Log %s does not have a filename", log->name);
		return log;
	}

	if (log->active)
		logfile_off(log, NULL);

	malloc_strcpy(&log->filename, arg);

	if (log->active)
		logfile_on(log, NULL);
	return log;
}

static Logfile *	logfile_kill (Logfile *log, char **args)
{
	if (!log)
	{
		say("KILL: You need to specify a logfile first");
		return NULL;
	}

	delete_logfile(log);
	return NULL;
}

static Logfile *	logfile_level (Logfile *log, char **args)
{
        char *arg = new_next_arg(*args, args);

	if (!log)
	{
		say("LEVEL: You need to specify a logfile first");
		return NULL;
	}

	log->level = parse_lastlog_level(arg);
	return log;
}

static Logfile *	logfile_list (Logfile *log, char **args)
{
	Logfile *l;
	char *targets = NULL;

	say("Logfiles:");
	for (l = logfiles; l; l = l->next)
	{
		targets = logfile_get_targets(l);
		say("Log %2d [%s] logging %s is %s, file %s server %d targets %s",
			l->refnum, l->name, logtype[l->type],
			onoff[l->active],
			l->filename ? l->filename : "<NONE>", 
			l->servref,
			targets ? targets : "<NONE>");
		new_free(&targets);
	}
	return log;
}

static Logfile *	logfile_mangle (Logfile *log, char **args)
{
	char *	arg = next_arg(*args, args);

	if (!log)
	{
		say("MANGLE: You need to specify a logfile first");
		return NULL;
	}

	if (!arg)
		say("MANGLE: This logfile mangles %s", log->mangle_desc);
	else
	{
		new_free(&log->mangle_desc);
		log->mangler = parse_mangle(arg, log->mangler, &log->mangle_desc);
		say("MANGLE: Now mangling %s", log->mangle_desc);
	}
	return log;
}

static Logfile *	logfile_name (Logfile *log, char **args)
{
        char *arg;

	if (!log)
	{
		say("NAME: You need to specify a logfile first");
		return NULL;
	}

        if (!(arg = next_arg(*args, args)))
                say("You must specify a name for the logfile!");
	else
        {
                /* /log name -  unsets the window name */
                if (!strcmp(arg, "-"))
                        new_free(&log->name);

                /* /log name to existing name -- ignore this. */
                else if (log->name && (my_stricmp(log->name, arg) == 0))
                        return log;

                else if (is_logfile_name_unique(arg))
                        malloc_strcpy(&log->name, arg);

                else
                        say("%s is not unique!", arg);
        }

        return log;
}

static Logfile *	logfile_new (Logfile *log, char **args)
{
	return new_logfile();
}

static Logfile *	logfile_off (Logfile *log, char **args)
{
	if (!log)
	{
		say("OFF: You need to specify a logfile first");
		return NULL;
	}

	if (!log->filename)
	{
		say("OFF: You need to specify a filename for this log first");
		return log;
	}

	time(&log->activity);
	do_log(0, log->filename, &log->log);
	log->active = 0;
	return log;
}

static Logfile *	logfile_on (Logfile *log, char **args)
{
	if (!log)
	{
		say("ON: You need to specify a logfile first");
		return NULL;
	}

	if (!log->filename)
	{
		say("ON: You need to specify a filename for this log first");
		return log;
	}

	time(&log->activity);
	do_log(1, log->filename, &log->log);
	log->active = 1;
	return log;
}

static Logfile *	logfile_refnum (Logfile *log, char **args)
{
	char *arg = next_arg(*args, args);

	log = get_log_by_desc(arg);
	return log;
}

static Logfile *	logfile_remove (Logfile *log, char **args)
{
	char 		*arg = next_arg(*args, args);
	char            *ptr;
	WNickList       *new_nl;
	int		i;

	if (!log)
	{
		say("REMOVE: You need to specify a logfile first");
		return NULL;
	}

        if (!arg)
                say("Remove: Remove nicknames/channels logged to this file");

	else while (arg)
        {
	        if ((ptr = strchr(arg, ',')) != NULL)
			*ptr++ = 0;

		if (log->type == LOG_TARGETS)
		{
		    if ((new_nl = (WNickList *)remove_from_list((List **)&(log->targets), arg)))
		    {
			say("Removed %s from log target list", new_nl->nick);
			new_free(&new_nl->nick);
			new_free((char **)&new_nl);
		    }
		    else
			say("%s is not on the list for this log!", arg);
		}
		else if (log->type == LOG_SERVERS || log->type == LOG_WINDOWS)
		{
		    int refnum = my_atol(ptr);

		    for (i = 0; i < MAX_TARGETS; i++)
		    {
			if (log->refnums[i] == refnum)
			{
				say("Removed %d to log refnum list", refnum);
				log->refnums[i] = -1;
				break;
			}
		    }
		    if (i >= MAX_TARGETS)
			say("%s is not on the refnum list for this log!", arg);
		}

		arg = ptr;
        }

        return log;
}

static Logfile *	logfile_rewrite (Logfile *log, char **args)
{
        char *arg = new_next_arg(*args, args);

	if (!log)
	{
		say("REWRITE: You need to specify a logfile first");
		return NULL;
	}

	malloc_strcpy(&log->rewrite, arg);
	return log;
}

static Logfile *	logfile_server (Logfile *log, char **args)
{
        char *arg = new_next_arg(*args, args);

	if (!log)
	{
		say("SERVER: You need to specify a logfile first");
		return NULL;
	}

	if (!is_number(arg))
		say("SERVER: The log's server needs to be a number");
	else
		log->servref = parse_server_index(arg, 1);

	return log;
}

static Logfile *	logfile_type (Logfile *log, char **args)
{
        char *arg = new_next_arg(*args, args);

	if (!log)
	{
		say("TYPE: You need to specify a logfile first");
		return NULL;
	}

	clean_log_targets(log);
	if (!my_strnicmp(arg, "SERVER", 1))
		log->type = LOG_SERVERS;
	else if (!my_strnicmp(arg, "WINDOW", 1))
		log->type = LOG_WINDOWS;
	else if (!my_strnicmp(arg, "TARGET", 1))
		log->type = LOG_TARGETS;
	else
		say("TYPE: Unknown type of log");

	return log;
}

typedef struct logfile_ops_T {
	const char *	command;
	logfile_func	func;
} logfile_ops;

static const logfile_ops options [] = {
	{ "ACTIVITY",	logfile_activity	},
	{ "ADD",	logfile_add		},
	{ "DESCRIBE",	logfile_describe	},
	{ "FILENAME",	logfile_filename	},
	{ "KILL",	logfile_kill		},
	{ "LEVEL",	logfile_level		},
	{ "LIST",	logfile_list		},
	{ "MANGLE",	logfile_mangle		},
	{ "NAME",	logfile_name		},
	{ "NEW",	logfile_new		},
	{ "OFF",	logfile_off		},
	{ "ON",		logfile_on		},
	{ "REFNUM",	logfile_refnum		},
	{ "REMOVE",	logfile_remove		},
	{ "REWRITE",	logfile_rewrite		},
	{ "SERVER",	logfile_server		},
	{ "TYPE",	logfile_type		},
	{ NULL,		NULL			}
};

BUILT_IN_COMMAND(logcmd)
{
        char    *arg;
        int     nargs = 0;
        Logfile	*log = NULL;

        while ((arg = next_arg(args, &args)))
        {
                int i;
                int len = strlen(arg);

                if (*arg == '-' || *arg == '/')         /* Ignore - or / */
                        arg++, len--;

                for (i = 0; options[i].func ; i++)
                {
                        if (!my_strnicmp(arg, options[i].command, len))
                        {
                                log = options[i].func(log, &args);
                                nargs++;
                                break;
                        }
                }

                if (!options[i].func)
                {
			Logfile *s_log;
                        if ((s_log = get_log_by_desc(arg)))
                        {
                                nargs++;
				log = s_log;
                        }
                        else
                                yell("LOG: Invalid option: [%s]", arg);
                }
        }

        if (!nargs)
                logfile_list(NULL, NULL);
}

/****************************************************************************/
void	add_to_logs (long winref, int servref, const char *target, int level, const char *orig_str)
{
	Logfile *log;
	int	i;

	for (log = logfiles; log; log = log->next)
	{
	    if (log->type == LOG_WINDOWS)
	    {
		for (i = 0; i < MAX_TARGETS; i++) {
		    if (log->refnums[i] == winref) {
			if (log->level && (log->level & level) == 0)
				continue;
			time(&log->activity);
			add_to_log(log->log, winref, orig_str, log->mangler, log->rewrite);
		    }
		}
	    }

	    if (log->type == LOG_SERVERS)
	    {
		for (i = 0; i < MAX_TARGETS; i++) {
		    if (log->refnums[i] == servref) {
			if (log->level && (log->level & level) == 0)
				continue;
			time(&log->activity);
			add_to_log(log->log, winref, orig_str, log->mangler, log->rewrite);
		    }
		}
	    }

	    else if (log->type == LOG_TARGETS)
	    {
		if (log->servref != NOSERV && (log->servref != servref))
			continue;

		if (log->level && (log->level & level) == 0)
			continue;

		if (log->targets && !target)
			continue;

		if (target && !find_in_list((List **)&log->targets, target, USE_WILDCARDS))
			continue;

		/* OK!  We want to log it now! */
		time(&log->activity);
		add_to_log(log->log, winref, orig_str, log->mangler, log->rewrite);
	    }
	}
}

/*****************************************************************************/
/* Used by function_logctl */
/*
 * $logctl(REFNUMS [ACTIVE|INACTIVE|ALL])
 * $logctl(REFNUM log-desc)
 * $logctl(ADD log-desc [target])
 * $logctl(DELETE log-desc [target])
 * $logctl(GET <refnum> [LIST]
 * $logctl(SET <refnum> [ITEM] [VALUE])
 * $logctl(MATCH [pattern])
 * $logctl(PMATCH [pattern])
 *
 * [LIST] and [ITEM] are one of the following
 *	REFNUM		The refnum for the log (GET only)
 *	NAME		The logical name for the log
 *	FILENAME	The filename this log writes to
 *	SERVER		The server this log associates with (-1 for any)
 *	TARGETS		All of the targets for this log
 *	LEVEL		The Lastlog Level for this log
 *	REWRITE		The rewrite rule for this log
 *	MANGLE		The mangle rule for this log
 *	STATUS		1 if log is on, 0 if log is off.
 *	TYPE		Either "TARGET", "WINDOW", or "SERVER"
 */
char *logctl	(char *input)
{
	char	*refstr;
	char	*listc;
	int	val;
	Logfile	*log;

	GET_STR_ARG(listc, input);
        if (!my_strnicmp(listc, "REFNUMS", 7)) {
		char *	retval = NULL;
		int	active;

		GET_STR_ARG(refstr, input);
		if (!my_stricmp(refstr, "ACTIVE"))
			active = 1;
		else if (!my_stricmp(refstr, "INACTIVE"))
			active = 0;
		else if (!my_stricmp(refstr, "ALL"))
			active = -1;
		else
			RETURN_EMPTY;

		for (log = logfiles; log; log = log->next)
		{
			if (active != -1 && active != log->active)
				continue;
			malloc_strcat_word(&retval, space, ltoa(log->refnum));
		}
		RETURN_MSTR(retval);
        } else if (!my_strnicmp(listc, "REFNUM", 6)) {
		GET_STR_ARG(refstr, input);
		if (!(log = get_log_by_desc(refstr)))
			RETURN_EMPTY;
		RETURN_INT(log->refnum);
        } else if (!my_strnicmp(listc, "ADD", 2)) {
		GET_STR_ARG(refstr, input);
		if (!(log = get_log_by_desc(refstr)))
			RETURN_EMPTY;
		logfile_add(log, &input);
		RETURN_INT(1);
        } else if (!my_strnicmp(listc, "DELETE", 2)) {
		GET_STR_ARG(refstr, input);
		if (!(log = get_log_by_desc(refstr)))
			RETURN_EMPTY;
		logfile_remove(log, &input);
		RETURN_INT(1);
        } else if (!my_strnicmp(listc, "GET", 2)) {
                GET_STR_ARG(refstr, input);
		if (!(log = get_log_by_desc(refstr)))
			RETURN_EMPTY;

                GET_STR_ARG(listc, input);
                if (!my_strnicmp(listc, "REFNUM", 1)) {
			RETURN_INT(log->refnum);
                } else if (!my_strnicmp(listc, "NAME", 3)) {
			RETURN_STR(log->name);
                } else if (!my_strnicmp(listc, "FILENAME", 3)) {
			RETURN_STR(log->filename);
                } else if (!my_strnicmp(listc, "SERVER", 3)) {
			RETURN_INT(log->servref);
                } else if (!my_strnicmp(listc, "TARGETS", 3)) {
			char *ret = logfile_get_targets(log);
			RETURN_MSTR(ret);
                } else if (!my_strnicmp(listc, "LEVEL", 3)) {
			char *ret = bits_to_lastlog_level(log->level);
			RETURN_STR(ret);
                } else if (!my_strnicmp(listc, "REWRITE", 3)) {
			RETURN_STR(log->rewrite);
                } else if (!my_strnicmp(listc, "MANGLE", 3)) {
			RETURN_STR(log->mangle_desc);
                } else if (!my_strnicmp(listc, "STATUS", 3)) {
			RETURN_INT(log->active);
                } else if (!my_strnicmp(listc, "TYPE", 3)) {
			RETURN_STR(logtype[log->type]);
		} else if (!my_strnicmp(listc, "ACTIVITY", 1)) {
			RETURN_INT(log->activity);
		}
        } else if (!my_strnicmp(listc, "SET", 1)) {
                GET_STR_ARG(refstr, input);
		if (!(log = get_log_by_desc(refstr)))
			RETURN_EMPTY;

		GET_STR_ARG(listc, input);
                if (!my_strnicmp(listc, "NAME", 3)) {
			logfile_name(log, &input);
			RETURN_INT(1);
                } else if (!my_strnicmp(listc, "FILENAME", 3)) {
			logfile_filename(log, &input);
			RETURN_INT(1);
                } else if (!my_strnicmp(listc, "SERVER", 3)) {
			logfile_server(log, &input);
			RETURN_INT(1);
                } else if (!my_strnicmp(listc, "TARGETS", 3)) {
			clean_log_targets(log);
			logfile_add(log, &input);
			RETURN_INT(1);
                } else if (!my_strnicmp(listc, "LEVEL", 3)) {
			logfile_level(log, &input);
			RETURN_INT(1);
                } else if (!my_strnicmp(listc, "REWRITE", 3)) {
			logfile_rewrite(log, &input);
			RETURN_INT(1);
                } else if (!my_strnicmp(listc, "MANGLE", 3)) {
			logfile_mangle(log, &input);
			RETURN_INT(1);
                } else if (!my_strnicmp(listc, "STATUS", 3)) {
			GET_INT_ARG(val, input);
			if (val)
				logfile_on(log, &input);
			else
				logfile_off(log, &input);
			RETURN_INT(1);
                } else if (!my_strnicmp(listc, "TYPE", 3)) {
			logfile_type(log, &input);
			RETURN_INT(1);
                } else if (!my_strnicmp(listc, "ACTIVITY", 1)) {
			logfile_activity(log, &input);
			RETURN_INT(1);
		}
        } else if (!my_strnicmp(listc, "MATCH", 1)) {
                RETURN_EMPTY;           /* Not implemented for now. */
        } else if (!my_strnicmp(listc, "PMATCH", 1)) {
                RETURN_EMPTY;           /* Not implemented for now. */
        } else
                RETURN_EMPTY;

        RETURN_EMPTY;
}

