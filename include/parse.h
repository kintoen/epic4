/*
 * parse.h
 *
 * Copyright 1993 Matthew Green
 * Copyright 1997 EPIC Software Labs
 * See the Copyright file for license information.
 */

#ifndef __parse_h__
#define __parse_h__

typedef struct {
	const char	*command;
	void 		(*inbound_handler) (char *, char **);
	void		(*outbound_handler) (char *);
	int		flags;
} protocol_command;
extern 	protocol_command rfc1459[];
extern	int		 num_protocol_cmds;

#define PROTO_NOQUOTE 	1 << 0
#define PROTO_DEPREC	1 << 1


	char	*PasteArgs 	(char **, int);
	void	parse_server 	(char *);
	int	is_channel	(const char *);

extern	char	*FromUserHost;
extern	int	doing_privmsg;

#endif
