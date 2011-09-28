/*****************************************************************************
  Copyright (c) 2011 Factor-SPE

  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the Free
  Software Foundation; either version 2 of the License, or (at your option)
  any later version.

  This program is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc., 59
  Temple Place - Suite 330, Boston, MA  02111-1307, USA.

  The full GNU General Public License is included in this distribution in the
  file called LICENSE.

  Authors: Vitalii Demianets <vitas@nppfactor.kiev.ua>

******************************************************************************/

#include <string.h>
#include <getopt.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>

#include "ctl_socket_client.h"
#include "log.h"

#ifdef  __LIBC_HAS_VERSIONSORT__
#define sorting_func    versionsort
#else
#define sorting_func    alphasort
#endif

static int get_index_die(const char *ifname, const char *doc, bool die)
{
    int r = if_nametoindex(ifname);
    if(0 == r)
    {
        fprintf(stderr,
                "Can't find index for %s %s. Not a valid interface.\n",
                doc, ifname);
        if(die)
            exit(1);
        return -1;
    }
    return r;
}

static inline int get_index(const char *ifname, const char *doc)
{
    return get_index_die(ifname, doc, true);
}

static inline int get_id(const char *str, const char *doc, unsigned int max_id)
{
    int id = strtol(str, NULL, 10);
    if((0 > id) || (max_id < id)
       || ((0 == id) && ('0' != str[0]))
      )
    {
        fprintf(stderr, "Bad %s %s\n", doc, str);
        return -1;
    }
    return id;
}

#define GET_NUM_FROM_PRIO(p) (__be16_to_cpu(p) & 0x0FFF)

#define BR_ID_FMT "%01hhX.%03hX.%02hhX:%02hhX:%02hhX:%02hhX:%02hhX:%02hhX"
#define BR_ID_ARGS(x) ((GET_PRIORITY_FROM_IDENTIFIER(x) >> 4) & 0x0F), \
    GET_NUM_FROM_PRIO((x).s.priority), \
    x.s.mac_address[0], x.s.mac_address[1], x.s.mac_address[2], \
    x.s.mac_address[3], x.s.mac_address[4], x.s.mac_address[5]

#define PRT_ID_FMT "%01hhX.%03hX"
#define PRT_ID_ARGS(x) ((GET_PRIORITY_FROM_IDENTIFIER(x) >> 4) & 0x0F), \
                       GET_NUM_FROM_PRIO(x)

#define BOOL_STR(x) ((x) ? "yes" : "no")

static int do_showbridge(const char *br_name)
{
    CIST_BridgeStatus s;
    char root_port_name[IFNAMSIZ];
    unsigned int root_portno;
    int br_index = get_index_die(br_name, "bridge", false);
    if(0 > br_index)
        return br_index;

    if(CTL_get_cist_bridge_status(br_index, &s, root_port_name))
        return -1;
    printf("%s CIST info\n", br_name);
    printf("  enabled         %s\n", BOOL_STR(s.enabled));
    printf("  bridge id       "BR_ID_FMT"\n", BR_ID_ARGS(s.bridge_id));
    printf("  designated root "BR_ID_FMT"\n", BR_ID_ARGS(s.designated_root));
    printf("  regional root   "BR_ID_FMT"\n", BR_ID_ARGS(s.regional_root));
    printf("  root port       ");
    if(0 != (root_portno = GET_NUM_FROM_PRIO(s.root_port_id)))
        printf("%s (#%u)\n", root_port_name, root_portno);
    else
        printf("none\n");
    printf("  path cost     %-10u ", s.root_path_cost);
    printf("internal path cost   %u\n", s.internal_path_cost);
    printf("  max age       %-10hhu ", s.root_max_age);
    printf("bridge max age       %hhu\n", s.bridge_max_age);
    printf("  forward delay %-10hhu ", s.root_forward_delay);
    printf("bridge forward delay %hhu\n", s.bridge_forward_delay);
    printf("  tx hold count %-10u ", s.tx_hold_count);
    printf("max hops             %hhu\n", s.max_hops);
    printf("  force protocol version     %u\n", s.protocol_version);
    printf("  time since topology change %u\n", s.time_since_topology_change);
    printf("  toplogy change count       %u\n", s.topology_change_count);
    printf("  topology change            %s\n", BOOL_STR(s.topology_change));

    return 0;
}

#define SYSFS_PATH_MAX 256
#define SYSFS_CLASS_NET "/sys/class/net"

static int isbridge(const struct dirent *entry)
{
    char path[SYSFS_PATH_MAX];
    int save_errno;
    bool result;
    struct stat st;

    snprintf(path, SYSFS_PATH_MAX, SYSFS_CLASS_NET "/%s/bridge",
             entry->d_name);
    save_errno = errno;
    result = (0 == stat(path, &st)) && S_ISDIR(st.st_mode);
    errno = save_errno;
    return result;
}

static int cmd_showbridge(int argc, char *const *argv)
{
    int i, count = 0;
    int r = 0;
    struct dirent **namelist;

    if(1 < argc)
    {
        count = argc - 1;
    }
    else
    {
        /* TODO: use versionsort, if available */
        count = scandir(SYSFS_CLASS_NET, &namelist, isbridge, sorting_func);
        if(0 > count)
        {
            fprintf(stderr, "Error getting list of all bridges\n");
            return -1;
        }
    }

    for(i = 0; i < count; ++i)
    {
        const char *name;
        if(1 < argc)
            name = argv[i + 1];
        else
            name = namelist[i]->d_name;

        int err = do_showbridge(name);
        if(err)
            r = err;
    }

    if(1 >= argc)
    {
        for(i = 0; i < count; ++i)
            free(namelist[i]);
        free(namelist);
    }

    return r;
}

static int cmd_showtree(int argc, char *const *argv)
{
    MSTI_BridgeStatus s;
    char root_port_name[IFNAMSIZ];
    unsigned int root_portno;
    int br_index = get_index(argv[1], "bridge");
    if(0 > br_index)
        return br_index;
    int mstid = get_id(argv[2], "mstid", MAX_MSTID);
    if(0 > mstid)
        return mstid;

    if(CTL_get_msti_bridge_status(br_index, mstid, &s, root_port_name))
        return -1;

    printf("%s MSTI %hu info\n", argv[1], mstid);
    printf("  bridge id          "BR_ID_FMT"\n", BR_ID_ARGS(s.bridge_id));
    printf("  regional root      "BR_ID_FMT"\n", BR_ID_ARGS(s.regional_root));
    printf("  root port          ");
    if(0 != (root_portno = GET_NUM_FROM_PRIO(s.root_port_id)))
        printf("%s (#%u)\n", root_port_name, root_portno);
    else
        printf("none\n");
    printf("  internal path cost %u\n", s.internal_path_cost);
    printf("  time since topology change %u\n", s.time_since_topology_change);
    printf("  toplogy change count       %u\n", s.topology_change_count);
    printf("  topology change            %s\n", BOOL_STR(s.topology_change));

    return 0;
}

#define STATE_STR(_state)                                        \
    ({                                                           \
        int _s = _state;                                         \
        char *_str = "unknown";                                  \
        switch(_s)                                               \
        {                                                        \
            case BR_STATE_DISABLED:                              \
            case BR_STATE_BLOCKING:                              \
            case BR_STATE_LISTENING: _str = "discarding"; break; \
            case BR_STATE_LEARNING:  _str = "learning"; break;   \
            case BR_STATE_FORWARDING:_str = "forwarding"; break; \
        }                                                        \
        _str;                                                    \
    })

#define SHORT_STATE_STR(_state)                            \
    ({                                                     \
        int _s = _state;                                   \
        char *_str = "unkn";                               \
        switch(_s)                                         \
        {                                                  \
            case BR_STATE_DISABLED:                        \
            case BR_STATE_BLOCKING:                        \
            case BR_STATE_LISTENING: _str = "disc"; break; \
            case BR_STATE_LEARNING:  _str = "lear"; break; \
            case BR_STATE_FORWARDING:_str = "forw"; break; \
        }                                                  \
        _str;                                              \
    })

#define ADMIN_P2P_STR(_state)                        \
    ({                                               \
        admin_p2p_t _s = _state;                     \
        char *_str = "unkn";                         \
        switch(_s)                                   \
        {                                            \
            case p2pForceFalse:_str = "no"; break;   \
            case p2pForceTrue: _str = "yes"; break;  \
            case p2pAuto:      _str = "auto"; break; \
        }                                            \
        _str;                                        \
    })

#define ROLE_STR(_role)                                     \
    ({                                                      \
        port_role_t _r = _role;                             \
        char *_str = "Unknown";                             \
        switch(_r)                                          \
        {                                                   \
            case roleRoot:      _str = "Root"; break;       \
            case roleDesignated:_str = "Designated"; break; \
            case roleAlternate: _str = "Alternate"; break;  \
            case roleBackup:    _str = "Backup"; break;     \
            case roleMaster:    _str = "Master"; break;     \
            case roleDisabled:  _str = "Disabled"; break;   \
        }                                                   \
        _str;                                               \
    })

#define SHORT_ROLE_STR(_role)                         \
    ({                                                \
        port_role_t _r = _role;                       \
        char *_str = "Unkn";                          \
        switch(_r)                                    \
        {                                             \
            case roleRoot:      _str = "Root"; break; \
            case roleDesignated:_str = "Desg"; break; \
            case roleAlternate: _str = "Altn"; break; \
            case roleBackup:    _str = "Back"; break; \
            case roleMaster:    _str = "Mstr"; break; \
            case roleDisabled:  _str = "Disa"; break; \
        }                                             \
        _str;                                         \
    })


static int detail = 0;

static int do_showport(int br_index, const char *bridge_name,
                       const char *port_name)
{
    CIST_PortStatus s;
    int r = 0;
    int port_index = get_index_die(port_name, "port", false);
    if(0 > port_index)
        return port_index;

    if((r = CTL_get_cist_port_status(br_index, port_index, &s)))
    {
        fprintf(stderr, "%s:%s Failed to get port state\n",
                bridge_name, port_name);
        return -1;
    }

    if(detail)
    {
        printf("%s:%s CIST info\n", bridge_name, port_name);
        printf("  enabled            %-23s ", BOOL_STR(s.enabled));
        printf("role                 %s\n", ROLE_STR(s.role));
        printf("  port id            "PRT_ID_FMT"                   ",
               PRT_ID_ARGS(s.port_id));
        printf("state                %s\n", STATE_STR(s.state));
        printf("  external port cost %-23u ", s.external_port_path_cost);
        printf("admin external cost  %u\n", s.admin_external_port_path_cost);
        printf("  internal port cost %-23u ", s.internal_port_path_cost);
        printf("admin internal cost  %u\n", s.admin_internal_port_path_cost);
        printf("  designated root    "BR_ID_FMT" ",
               BR_ID_ARGS(s.designated_root));
        printf("dsgn external cost   %u\n", s.designated_external_cost);
        printf("  dsgn regional root "BR_ID_FMT" ",
               BR_ID_ARGS(s.designated_regional_root));
        printf("dsgn internal cost   %u\n", s.designated_internal_cost);
        printf("  designated bridge  "BR_ID_FMT" ",
               BR_ID_ARGS(s.designated_bridge));
        printf("designated port      "PRT_ID_FMT"\n",
               PRT_ID_ARGS(s.designated_port));
        printf("  admin edge port    %-23s ", BOOL_STR(s.admin_edge_port));
        printf("auto edge port       %s\n", BOOL_STR(s.auto_edge_port));
        printf("  oper edge port     %-23s ", BOOL_STR(s.oper_edge_port));
        printf("toplogy change ack   %s\n", BOOL_STR(s.tc_ack));
        printf("  point-to-point     %-23s ", BOOL_STR(s.oper_p2p));
        printf("admin point-to-point %s\n", ADMIN_P2P_STR(s.admin_p2p));
        printf("  restricted role    %-23s ", BOOL_STR(s.restricted_role));
        printf("restricted TCN       %s\n", BOOL_STR(s.restricted_tcn));
        printf("  port hello time    %-23hhu ", s.port_hello_time);
        printf("disputed             %s\n", BOOL_STR(s.disputed));
    }
    else
    {
        printf("%c%c %-5s "PRT_ID_FMT" %4s "BR_ID_FMT" "BR_ID_FMT" "PRT_ID_FMT" %s\n",
               (s.oper_p2p) ? ' ' : '*',
               (s.oper_edge_port) ? 'E' : ' ',
               port_name,
               PRT_ID_ARGS(s.port_id),
               s.enabled ? SHORT_STATE_STR(s.state) : "down",
               BR_ID_ARGS(s.designated_root),
               BR_ID_ARGS(s.designated_bridge),
               PRT_ID_ARGS(s.designated_port),
               SHORT_ROLE_STR(s.role));
    }
    return 0;
}

static int not_dot_dotdot(const struct dirent *entry)
{
    const char *n = entry->d_name;

    return !('.' == n[0] && (0 == n[1] || ('.' == n[1] && 0 == n[2])));
}

static int cmd_showport(int argc, char *const *argv)
{
    int r = 0;

    int br_index = get_index(argv[1], "bridge");
    if(0 > br_index)
        return br_index;

    int i, count = 0;
    struct dirent **namelist;

    if(2 < argc)
    {
        count = argc - 2;
    }
    else
    {
        char buf[SYSFS_PATH_MAX];
        snprintf(buf, sizeof(buf), SYSFS_CLASS_NET "/%s/brif", argv[1]);
        /* TODO: use versionsort, if available */
        count = scandir(buf, &namelist, not_dot_dotdot, sorting_func);
        if(0 > count)
        {
            fprintf(stderr, "Error getting list of all ports of bridge %s\n",
                    argv[1]);
            return -1;
        }
    }

    for(i = 0; i < count; ++i)
    {
        const char *name;
        if(2 < argc)
            name = argv[i + 2];
        else
            name = namelist[i]->d_name;

        int err = do_showport(br_index, argv[1], name);
        if(err)
            r = err;
    }

    if(2 >= argc)
    {
        for(i = 0; i < count; ++i)
            free(namelist[i]);
        free(namelist);
    }

    return r;
}

static int cmd_showportdetail(int argc, char *const *argv)
{
    detail = 1;
    return cmd_showport(argc, argv);
}

static int cmd_showtreeport(int argc, char *const *argv)
{
    MSTI_PortStatus s;
    int br_index = get_index(argv[1], "bridge");
    if(0 > br_index)
        return br_index;
    int port_index = get_index(argv[2], "port");
    if(0 > port_index)
        return port_index;
    int mstid = get_id(argv[3], "mstid", MAX_MSTID);
    if(0 > mstid)
        return mstid;

    if(CTL_get_msti_port_status(br_index, port_index, mstid, &s))
        return -1;

    printf("%s:%s MSTI %hu info\n", argv[1], argv[2], mstid);
    printf("  role               %-23s ", ROLE_STR(s.role));
    printf("port id              "PRT_ID_FMT"\n", PRT_ID_ARGS(s.port_id));
    printf("  state              %-23s ", STATE_STR(s.state));
    printf("disputed             %s\n", BOOL_STR(s.disputed));
    printf("  internal port cost %-23u ", s.internal_port_path_cost);
    printf("admin internal cost  %u\n", s.admin_internal_port_path_cost);
    printf("  dsgn regional root "BR_ID_FMT" ",
           BR_ID_ARGS(s.designated_regional_root));
    printf("dsgn internal cost   %u\n", s.designated_internal_cost);
    printf("  designated bridge  "BR_ID_FMT" ",
           BR_ID_ARGS(s.designated_bridge));
    printf("designated port      "PRT_ID_FMT"\n",
           PRT_ID_ARGS(s.designated_port));

    return 0;
}

static unsigned int getuint(const char *s)
{
    char *end;
    long l;
    l = strtoul(s, &end, 0);
    if(0 == *s || 0 != *end || INT_MAX < l)
    {
        fprintf(stderr, "Invalid unsigned int arg %s\n", s);
        exit(1);
    }
    return l;
}

static int getenum(const char *s, const char *opt[])
{
    int i;
    for(i = 0; opt[i] != NULL; ++i)
        if(0 == strcmp(s, opt[i]))
            return i;

    fprintf(stderr, "Invalid argument %s: expecting one of ", s);
    for(i = 0; opt[i] != NULL; ++i)
        fprintf(stderr, "%s%s", opt[i], (opt[i + 1] ? ", " : "\n"));

    exit(1);
}

static int getyesno(const char *s, const char *yes, const char *no)
{
    /* Reverse yes and no so error message looks more normal */
    const char *opt[] = { yes, no, NULL };
    return 1 - getenum(s, opt);
}

static int cmd_setmstconfid(int argc, char *const *argv)
{
    int br_index = get_index(argv[1], "bridge");
    if(0 > br_index)
        return br_index;
    unsigned int revision = getuint(argv[2]);
    if(revision > 0xFFFF)
    {
        fprintf(stderr, "Bad revision %s\n", argv[2]);
        return -1;
    }
    return CTL_set_mstconfid(br_index, revision, argv[3]);
}

#define set_bridge_cfg(field, value)                       \
    ({                                                     \
        CIST_BridgeConfig c;                               \
        memset(&c, 0, sizeof(c));                          \
        c.field = value;                                   \
        c.set_ ## field = true;                            \
        int r = CTL_set_cist_bridge_config(br_index, &c);  \
        if(r)                                              \
            printf("Couldn't change bridge " #field "\n"); \
        r;                                                 \
    })

#define set_port_cfg(field, value)                                  \
    ({                                                              \
        CIST_PortConfig c;                                          \
        memset(&c, 0, sizeof(c));                                   \
        c.field = value;                                            \
        c.set_ ## field = true;                                     \
        int r = CTL_set_cist_port_config(br_index, port_index, &c); \
        if(r)                                                       \
            printf("Couldn't change port " #field "\n");            \
        r;                                                          \
    })

#define set_tree_port_cfg(field, value)                                    \
    ({                                                                     \
        MSTI_PortConfig c;                                                 \
        memset(&c, 0, sizeof(c));                                          \
        c.field = value;                                                   \
        c.set_ ## field = true;                                            \
        int r = CTL_set_msti_port_config(br_index, port_index, mstid, &c); \
        if(r)                                                              \
            printf("Couldn't change per-tree port " #field "\n");          \
        r;                                                                 \
    })

static int cmd_setbridgemaxage(int argc, char *const *argv)
{
    int br_index = get_index(argv[1], "bridge");
    if(0 > br_index)
        return br_index;
    unsigned int max_age = getuint(argv[2]);
    if(max_age > 255)
        max_age = 255;
    return set_bridge_cfg(bridge_max_age, max_age);
}

static int cmd_setbridgefdelay(int argc, char *const *argv)
{
    int br_index = get_index(argv[1], "bridge");
    if(0 > br_index)
        return br_index;
    unsigned int forward_delay = getuint(argv[2]);
    if(forward_delay > 255)
        forward_delay = 255;
    return set_bridge_cfg(bridge_forward_delay, forward_delay);
}

static int cmd_setbridgemaxhops(int argc, char *const *argv)
{
    int br_index = get_index(argv[1], "bridge");
    if(0 > br_index)
        return br_index;
    unsigned int max_hops = getuint(argv[2]);
    if(max_hops > 255)
        max_hops = 255;
    return set_bridge_cfg(max_hops, max_hops);
}

static int cmd_setbridgeforcevers(int argc, char *const *argv)
{
    int br_index = get_index(argv[1], "bridge");
    if(0 > br_index)
        return br_index;
    const char *opts[] = { "stp", "rstp", "mstp", NULL };
    int vals[] = { protoSTP, protoRSTP, protoMSTP };
    return set_bridge_cfg(protocol_version, vals[getenum(argv[2], opts)]);
}

static int cmd_setbridgetxholdcount(int argc, char *const *argv)
{
    int br_index = get_index(argv[1], "bridge");
    if(0 > br_index)
        return br_index;
    return set_bridge_cfg(tx_hold_count, getuint(argv[2]));
}

static int cmd_settreeprio(int argc, char *const *argv)
{
    int br_index = get_index(argv[1], "bridge");
    if(0 > br_index)
        return br_index;
    int mstid = get_id(argv[2], "mstid", MAX_MSTID);
    if(0 > mstid)
        return mstid;
    unsigned int prio = getuint(argv[3]);
    if(prio > 255)
        prio = 255;
    return CTL_set_msti_bridge_config(br_index,  mstid, prio);
}

static int cmd_setportpathcost(int argc, char *const *argv)
{
    int br_index = get_index(argv[1], "bridge");
    if(0 > br_index)
        return br_index;
    int port_index = get_index(argv[2], "port");
    if(0 > port_index)
        return port_index;
    return set_port_cfg(admin_external_port_path_cost, getuint(argv[3]));
}

static int cmd_setportadminedge(int argc, char *const *argv)
{
    int br_index = get_index(argv[1], "bridge");
    if(0 > br_index)
        return br_index;
    int port_index = get_index(argv[2], "port");
    if(0 > port_index)
        return port_index;
    return set_port_cfg(admin_edge_port, getyesno(argv[3], "yes", "no"));
}

static int cmd_setportautoedge(int argc, char *const *argv)
{
    int br_index = get_index(argv[1], "bridge");
    if(0 > br_index)
        return br_index;
    int port_index = get_index(argv[2], "port");
    if(0 > port_index)
        return port_index;
    return set_port_cfg(auto_edge_port, getyesno(argv[3], "yes", "no"));
}

static int cmd_setportp2p(int argc, char *const *argv)
{
    int br_index = get_index(argv[1], "bridge");
    if(0 > br_index)
        return br_index;
    int port_index = get_index(argv[2], "port");
    if(0 > port_index)
        return port_index;
    const char *opts[] = { "no", "yes", "auto", NULL };
    int vals[] = { p2pForceFalse, p2pForceTrue, p2pAuto };
    return set_port_cfg(admin_p2p, vals[getenum(argv[3], opts)]);
}

static int cmd_setportrestrrole(int argc, char *const *argv)
{
    int br_index = get_index(argv[1], "bridge");
    if(0 > br_index)
        return br_index;
    int port_index = get_index(argv[2], "port");
    if(0 > port_index)
        return port_index;
    return set_port_cfg(restricted_role, getyesno(argv[3], "yes", "no"));
}

static int cmd_setportrestrtcn(int argc, char *const *argv)
{
    int br_index = get_index(argv[1], "bridge");
    if(0 > br_index)
        return br_index;
    int port_index = get_index(argv[2], "port");
    if(0 > port_index)
        return port_index;
    return set_port_cfg(restricted_tcn, getyesno(argv[3], "yes", "no"));
}

static int cmd_settreeportprio(int argc, char *const *argv)
{
    int br_index = get_index(argv[1], "bridge");
    if(0 > br_index)
        return br_index;
    int port_index = get_index(argv[2], "port");
    if(0 > port_index)
        return port_index;
    int mstid = get_id(argv[3], "mstid", MAX_MSTID);
    if(0 > mstid)
        return mstid;
    unsigned int prio = getuint(argv[4]);
    if(prio > 255)
        prio = 255;
    return set_tree_port_cfg(port_priority, prio);
}

static int cmd_settreeportcost(int argc, char *const *argv)
{
    int br_index = get_index(argv[1], "bridge");
    if(0 > br_index)
        return br_index;
    int port_index = get_index(argv[2], "port");
    if(0 > port_index)
        return port_index;
    int mstid = get_id(argv[3], "mstid", MAX_MSTID);
    if(0 > mstid)
        return mstid;
    return set_tree_port_cfg(admin_internal_port_path_cost, getuint(argv[4]));
}

static int cmd_portmcheck(int argc, char *const *argv)
{
    int br_index = get_index(argv[1], "bridge");
    if(0 > br_index)
        return br_index;
    int port_index = get_index(argv[2], "port");
    if(0 > port_index)
        return port_index;
    return CTL_port_mcheck(br_index, port_index);
}

static int cmd_debuglevel(int argc, char *const *argv)
{
    return CTL_set_debug_level(getuint(argv[1]));
}

static int cmd_showmstilist(int argc, char *const *argv)
{
    int br_index = get_index(argv[1], "bridge");
    if(0 > br_index)
        return br_index;
    int num_mstis = 0, i;
    __u16 mstids[MAX_IMPLEMENTATION_MSTIS + 1]; /* +1 - for the CIST */

    if(CTL_get_mstilist(br_index, &num_mstis, mstids))
        return -1;

    printf("%s list of known MSTIs:\n", argv[1]);
    for(i = 0; i < num_mstis; ++i)
        printf(" %hu", mstids[i]);
    printf("\n");

    return 0;
}

static int cmd_showmstconfid(int argc, char *const *argv)
{
    mst_configuration_identifier_t cfgid;
    int br_index = get_index(argv[1], "bridge");
    if(0 > br_index)
        return br_index;
    int i;

    if(CTL_get_mstconfid(br_index, &cfgid))
        return -1;

    printf("%s MST Configuration Identifier:\n", argv[1]);
    printf("  Format Selector:      %hhu\n", cfgid.s.selector);
    printf("  Configuration Name:   %.*s\n", CONFIGURATION_NAME_LEN,
           cfgid.s.configuration_name);
    printf("  Revision Level:       %hu\n",
           __be16_to_cpu(cfgid.s.revision_level));
    printf("  Configuration Digest: ");
    for(i = 0; i < CONFIGURATION_DIGEST_LEN; ++i)
        printf("%02hhX", cfgid.s.configuration_digest[i]);
    printf("\n");

    return 0;
}

static int cmd_createtree(int argc, char *const *argv)
{
    int br_index = get_index(argv[1], "bridge");
    if(0 > br_index)
        return br_index;
    int mstid = get_id(argv[2], "mstid", MAX_MSTID);
    if(0 > mstid)
        return mstid;
    return CTL_create_msti(br_index, mstid);
}

static int cmd_deletetree(int argc, char *const *argv)
{
    int br_index = get_index(argv[1], "bridge");
    if(0 > br_index)
        return br_index;
    int mstid = get_id(argv[2], "mstid", MAX_MSTID);
    if(0 > mstid)
        return mstid;
    return CTL_delete_msti(br_index, mstid);
}

static int cmd_showvid2fid(int argc, char *const *argv)
{
    __u16 vid2fid[MAX_VID + 2];
    int br_index = get_index(argv[1], "bridge");
    if(0 > br_index)
        return br_index;

    if(CTL_get_vids2fids(br_index, vid2fid))
        return -1;

    printf("%s VID-to-FID allocation table:\n", argv[1]);
    int i, cur_fid;
    int interval_count;
    vid2fid[MAX_VID + 1] = 0xFFFF; /* helps to finalize last interval */
    do{
        cur_fid = vid2fid[0];
        for(i = 1; i <= MAX_VID; ++i)
            if(cur_fid > vid2fid[i])
                cur_fid = vid2fid[i];
        if(cur_fid > MAX_FID)
            break;
        printf("  FID %u:", cur_fid);
        for(i = 0, interval_count = 0; i <= (MAX_VID + 1); ++i)
        {
            if(cur_fid != vid2fid[i])
            {
                if(interval_count)
                {
                    printf(" %u", i - interval_count);
                    if(1 < interval_count)
                        printf("-%u", i - 1);
                    interval_count = 0;
                }
                continue;
            }
            vid2fid[i] = 0xFFFF;
            ++interval_count;
        }
        printf("\n");
    }while(true);

    return 0;
}

static int cmd_showfid2mstid(int argc, char *const *argv)
{
    __u16 fid2mstid[MAX_FID + 2];
    int br_index = get_index(argv[1], "bridge");
    if(0 > br_index)
        return br_index;

    if(CTL_get_fids2mstids(br_index, fid2mstid))
        return -1;

    printf("%s FID-to-MSTID allocation table:\n", argv[1]);
    int i, cur_mstid;
    int interval_count;
    fid2mstid[MAX_FID + 1] = 0xFFFF; /* helps to finalize last interval */
    do{
        cur_mstid = fid2mstid[0];
        for(i = 1; i <= MAX_FID; ++i)
            if(cur_mstid > fid2mstid[i])
                cur_mstid = fid2mstid[i];
        if(cur_mstid > MAX_MSTID)
            break;
        printf("  MSTID %u:", cur_mstid);
        for(i = 0, interval_count = 0; i <= (MAX_FID + 1); ++i)
        {
            if(cur_mstid != fid2mstid[i])
            {
                if(interval_count)
                {
                    printf(" %u", i - interval_count);
                    if(1 < interval_count)
                        printf("-%u", i - 1);
                    interval_count = 0;
                }
                continue;
            }
            fid2mstid[i] = 0xFFFF;
            ++interval_count;
        }
        printf("\n");
    }while(true);

    return 0;
}

static int cmd_setvid2fid(int argc, char *const *argv)
{
    int br_index = get_index(argv[1], "bridge");
    if(0 > br_index)
        return br_index;
    int vid = get_id(argv[2], "VID", MAX_VID);
    if(0 > vid)
        return vid;
    if(0 == vid)
    {
        fprintf(stderr, "Bad VID %s\n", argv[2]);
        return -1;
    }
    int fid = get_id(argv[3], "FID", MAX_FID);
    if(0 > fid)
        return fid;
    return CTL_set_vid2fid(br_index, vid, fid);
}

static int cmd_setfid2mstid(int argc, char *const *argv)
{
    int br_index = get_index(argv[1], "bridge");
    if(0 > br_index)
        return br_index;
    int fid = get_id(argv[2], "FID", MAX_FID);
    if(0 > fid)
        return fid;
    int mstid = get_id(argv[3], "mstid", MAX_MSTID);
    if(0 > mstid)
        return mstid;
    return CTL_set_fid2mstid(br_index, fid, mstid);
}

struct command
{
    int nargs;
    int optargs;
    const char *name;
    int (*func) (int argc, char *const *argv);
    const char *format;
    const char *help;
};

static const struct command commands[] =
{
    /* Show global bridge */
    {0, 32, "showbridge", cmd_showbridge,
     "[<bridge> ... ]", "Show bridge state for the CIST"},
    {1, 0, "showmstilist", cmd_showmstilist,
     "<bridge>", "Show list of registered MSTIs"},
    {1, 0, "showmstconfid", cmd_showmstconfid,
     "<bridge>", "Show MST ConfigId"},
    {1, 0, "showvid2fid", cmd_showvid2fid,
     "<bridge>", "Show VID-to-FID allocation table"},
    {1, 0, "showfid2mstid", cmd_showfid2mstid,
     "<bridge>", "Show FID-to-MSTID allocation table"},
    /* Show global port */
    {1, 32, "showport", cmd_showport,
     "<bridge> [<port> ... ]", "Show port state for the CIST"},
    {1, 32, "showportdetail", cmd_showportdetail,
     "<bridge> [<port> ... ]", "Show port detailed state for the CIST"},
    /* Show tree bridge */
    {2, 0, "showtree", cmd_showtree,
     "<bridge> <mstid>", "Show bridge state for the given MSTI"},
    /* Show tree port */
    {3, 0, "showtreeport", cmd_showtreeport,
     "<bridge> <port> <mstid>", "Show port detailed state for the given MSTI"},

    /* Set global bridge */
    {3, 0, "setmstconfid", cmd_setmstconfid,
     "<bridge> <revision> <name>",
     "Set MST ConfigId elements: Revision Level (0-65535) and Name"},
    {3, 0, "setvid2fid", cmd_setvid2fid,
     "<bridge> <VID> <FID>", "Set VID-to-FID allocation"},
    {3, 0, "setfid2mstid", cmd_setfid2mstid,
     "<bridge> <FID> <mstid>", "Set FID-to-MSTID allocation"},
    {2, 0, "setmaxage", cmd_setbridgemaxage,
     "<bridge> <max_age>", "Set bridge max age (6-40)"},
    {2, 0, "setfdelay", cmd_setbridgefdelay,
     "<bridge> <fwd_delay>", "Set bridge forward delay (4-30)"},
    {2, 0, "setmaxhops", cmd_setbridgemaxhops,
     "<bridge> <max_hops>", "Set bridge max hops (6-40)"},
    {2, 0, "setforcevers", cmd_setbridgeforcevers,
     "<bridge> {mstp|rstp|stp}", "Force Spanning Tree protocol version"},
    {2, 0, "settxholdcount", cmd_setbridgetxholdcount,
     "<bridge> <tx_hold_count>", "Set bridge transmit hold count (1-10)"},
    /* Set tree bridge */
    {2, 0, "createtree", cmd_createtree,
     "<bridge> <mstid>", "Create new MSTI"},
    {2, 0, "deletetree", cmd_deletetree,
     "<bridge> <mstid>", "Delete existing MSTI"},
    {3, 0, "settreeprio", cmd_settreeprio,
     "<bridge> <mstid> <priority>",
     "Set bridge priority (0-15) for the given MSTI"},
    /* Set global port */
    {3, 0, "setportpathcost", cmd_setportpathcost,
     "<bridge> <port> <cost>",
     "Set port external path cost for the CIST (0 = auto)"},
    {3, 0, "setportadminedge", cmd_setportadminedge,
     "<bridge> <port> {yes|no}", "Set initial edge state"},
    {3, 0, "setportautoedge", cmd_setportautoedge,
     "<bridge> <port> {yes|no}", "Enable auto transition to/from edge state"},
    {3, 0, "setportp2p", cmd_setportp2p,
     "<bridge> <port> {yes|no|auto}", "Set p2p detection mode"},
    {3, 0, "setportrestrrole", cmd_setportrestrrole,
     "<bridge> <port> {yes|no}", "Restrict port ability to take Root role"},
    {3, 0, "setportrestrtcn", cmd_setportrestrtcn,
     "<bridge> <port> {yes|no}",
     "Restrict port ability to propagate received TCNs"},
    {2, 0, "portmcheck", cmd_portmcheck,
     "<bridge> <port>", "Try to get back from STP to rapid (RSTP/MSTP) mode"},
    /* Set tree port */
    {4, 0, "settreeportprio", cmd_settreeportprio,
     "<bridge> <port> <mstid> <priority>",
     "Set port priority (0-15) for the given MSTI"},
    {4, 0, "settreeportcost", cmd_settreeportcost,
     "<bridge> <port> <mstid> <cost>",
     "Set port internal path cost for the given MSTI (0 = auto)"},

    /* Other */
    {1, 0, "debuglevel", cmd_debuglevel, "<level>", "Level of verbosity"},
};

static const struct command *command_lookup(const char *cmd)
{
    int i;

    for(i = 0; i < COUNT_OF(commands); ++i)
    {
        if(!strcmp(cmd, commands[i].name))
            return &commands[i];
    }

    return NULL;
}

static void command_helpall(void)
{
    int i;

    for(i = 0; i < COUNT_OF(commands); ++i)
    {
        printf("-%s:\n   %-16s %s\n", commands[i].help, commands[i].name,
               commands[i].format);
    }
}

static void help(void)
{
    printf("Usage: mstpctl [commands]\n");
    printf("commands:\n");
    command_helpall();
}

#define PACKAGE_VERSION2(v, b) "mstp, " #v "-" #b
#define PACKAGE_VERSION(v, b) PACKAGE_VERSION2(v, b)

int main(int argc, char *const *argv)
{
    const struct command *cmd;
    int f;
    static const struct option options[] =
    {
        {.name = "help",    .val = 'h'},
        {.name = "version", .val = 'V'},
        {0}
    };

    while(EOF != (f = getopt_long(argc, argv, "Vh", options, NULL)))
        switch(f)
        {
            case 'h':
                help();
                return 0;
            case 'V':
                printf("%s\n", PACKAGE_VERSION(VERSION, BUILD));
                return 0;
            default:
                fprintf(stderr, "Unknown option '%c'\n", f);
                goto help;
        }

    if(argc == optind)
        goto help;

    if(ctl_client_init())
    {
        fprintf(stderr, "can't setup control connection\n");
        return 1;
    }

    argc -= optind;
    argv += optind;
    if(NULL == (cmd = command_lookup(argv[0])))
    {
        fprintf(stderr, "never heard of command [%s]\n", argv[0]);
        goto help;
    }

    if(argc < cmd->nargs + 1 || argc > cmd->nargs + cmd->optargs + 1)
    {
        printf("Incorrect number of arguments for command\n");
        printf("Usage: mstpctl %s %s\n  %s\n",
               cmd->name, cmd->format, cmd->help);
        return 1;
    }

    return cmd->func(argc, argv);

help:
    help();
    return 1;
}

/* Implementation of client-side functions */
CLIENT_SIDE_FUNCTION(get_cist_bridge_status)
CLIENT_SIDE_FUNCTION(get_msti_bridge_status)
CLIENT_SIDE_FUNCTION(set_cist_bridge_config)
CLIENT_SIDE_FUNCTION(set_msti_bridge_config)
CLIENT_SIDE_FUNCTION(get_cist_port_status)
CLIENT_SIDE_FUNCTION(get_msti_port_status)
CLIENT_SIDE_FUNCTION(set_cist_port_config)
CLIENT_SIDE_FUNCTION(set_msti_port_config)
CLIENT_SIDE_FUNCTION(port_mcheck)
CLIENT_SIDE_FUNCTION(set_debug_level)
CLIENT_SIDE_FUNCTION(get_mstilist)
CLIENT_SIDE_FUNCTION(create_msti)
CLIENT_SIDE_FUNCTION(delete_msti)
CLIENT_SIDE_FUNCTION(get_mstconfid)
CLIENT_SIDE_FUNCTION(set_mstconfid)
CLIENT_SIDE_FUNCTION(get_vids2fids)
CLIENT_SIDE_FUNCTION(get_fids2mstids)
CLIENT_SIDE_FUNCTION(set_vid2fid)
CLIENT_SIDE_FUNCTION(set_fid2mstid)
CLIENT_SIDE_FUNCTION(set_vids2fids)
CLIENT_SIDE_FUNCTION(set_fids2mstids)

/*********************** Logging *********************/

void Dprintf(int level, const char *fmt, ...)
{
    char logbuf[LOG_STRING_LEN];
    logbuf[sizeof(logbuf) - 1] = 0;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(logbuf, sizeof(logbuf) - 1, fmt, ap);
    va_end(ap);
    printf("%s\n", logbuf);
}
