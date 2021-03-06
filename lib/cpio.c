/** \ingroup payload
 * \file lib/cpio.c
 *  Handle cpio payloads within rpm packages.
 *
 * \warning FIXME: We don't translate between cpio and system mode bits! These
 * should both be the same, but really odd things are going to happen if
 * that's not true!
 */

#include "system.h"

#if MAJOR_IN_MKDEV
#include <sys/mkdev.h>
#elif MAJOR_IN_SYSMACROS
#include <sys/sysmacros.h>
#else
#include <sys/types.h> /* already included from system.h */
#endif
#include <errno.h>

#include <rpm/rpmio.h>
#include <rpm/rpmlog.h>

#include "lib/cpio.h"
#include "lib/fsm.h"

#include "debug.h"


/**
 * Convert string to unsigned integer (with buffer size check).
 * @param str		input string
 * @retval endptr	address of 1st character not processed
 * @param base		numerical conversion base
 * @param num		max no. of bytes to read
 * @return		converted integer
 */
static unsigned long strntoul(const char *str,char **endptr, int base, size_t num)
{
    char buf[num+1], * end;
    unsigned long ret;

    strncpy(buf, str, num);
    buf[num] = '\0';

    ret = strtoul(buf, &end, base);
    if (*end != '\0')
	*endptr = ((char *)str) + (end - buf);	/* XXX discards const */
    else
	*endptr = ((char *)str) + strlen(buf);

    return ret;
}

#define GET_NUM_FIELD(phys, log) \
	\
	log = strntoul(phys, &end, 16, sizeof(phys)); \
	\
	if ( (end - phys) != sizeof(phys) ) return CPIOERR_BAD_HEADER;
#define SET_NUM_FIELD(phys, val, space) \
	sprintf(space, "%8.8lx", (unsigned long) (val)); \
	\
	memcpy(phys, space, 8) \

int cpioTrailerWrite(FSM_t fsm)
{
    struct cpioCrcPhysicalHeader * hdr =
	(struct cpioCrcPhysicalHeader *)fsm->rdbuf;
    int rc;

    memset(hdr, '0', PHYS_HDR_SIZE);
    memcpy(hdr->magic, CPIO_NEWC_MAGIC, sizeof(hdr->magic));
    memcpy(hdr->nlink, "00000001", 8);
    memcpy(hdr->namesize, "0000000b", 8);
    memcpy(fsm->rdbuf + PHYS_HDR_SIZE, CPIO_TRAILER, sizeof(CPIO_TRAILER));

    /* XXX DWRITE uses rdnb for I/O length. */
    fsm->rdnb = PHYS_HDR_SIZE + sizeof(CPIO_TRAILER);
    rc = fsmNext(fsm, FSM_DWRITE);

    /*
     * GNU cpio pads to 512 bytes here, but we don't. This may matter for
     * tape device(s) and/or concatenated cpio archives. <shrug>
     */
    if (!rc)
	rc = fsmNext(fsm, FSM_PAD);

    return rc;
}

int cpioHeaderWrite(FSM_t fsm, struct stat * st)
{
    struct cpioCrcPhysicalHeader * hdr = (struct cpioCrcPhysicalHeader *)fsm->rdbuf;
    char field[64];
    size_t len;
    dev_t dev;
    int rc = 0;

    memcpy(hdr->magic, CPIO_NEWC_MAGIC, sizeof(hdr->magic));
    SET_NUM_FIELD(hdr->inode, st->st_ino, field);
    SET_NUM_FIELD(hdr->mode, st->st_mode, field);
    SET_NUM_FIELD(hdr->uid, st->st_uid, field);
    SET_NUM_FIELD(hdr->gid, st->st_gid, field);
    SET_NUM_FIELD(hdr->nlink, st->st_nlink, field);
    SET_NUM_FIELD(hdr->mtime, st->st_mtime, field);
    SET_NUM_FIELD(hdr->filesize, st->st_size, field);

    dev = major(st->st_dev); SET_NUM_FIELD(hdr->devMajor, dev, field);
    dev = minor(st->st_dev); SET_NUM_FIELD(hdr->devMinor, dev, field);
    dev = major(st->st_rdev); SET_NUM_FIELD(hdr->rdevMajor, dev, field);
    dev = minor(st->st_rdev); SET_NUM_FIELD(hdr->rdevMinor, dev, field);

    len = strlen(fsm->path) + 1; SET_NUM_FIELD(hdr->namesize, len, field);
    memcpy(hdr->checksum, "00000000", 8);
    memcpy(fsm->rdbuf + PHYS_HDR_SIZE, fsm->path, len);

    /* XXX DWRITE uses rdnb for I/O length. */
    fsm->rdnb = PHYS_HDR_SIZE + len;
    rc = fsmNext(fsm, FSM_DWRITE);
    if (!rc && fsm->rdnb != fsm->wrnb)
	rc = CPIOERR_WRITE_FAILED;
    if (!rc)
	rc = fsmNext(fsm, FSM_PAD);
    return rc;
}

int cpioHeaderRead(FSM_t fsm, struct stat * st)
{
    struct cpioCrcPhysicalHeader hdr;
    int nameSize;
    char * end;
    unsigned int major, minor;
    int rc = 0;
    char * path = NULL;

    fsm->wrlen = PHYS_HDR_SIZE;
    rc = fsmNext(fsm, FSM_DREAD);
    if (!rc && fsm->rdnb != fsm->wrlen)
	rc = CPIOERR_READ_FAILED;
    if (rc) return rc;
    memcpy(&hdr, fsm->wrbuf, fsm->rdnb);

    if (strncmp(CPIO_CRC_MAGIC, hdr.magic, sizeof(CPIO_CRC_MAGIC)-1) &&
	strncmp(CPIO_NEWC_MAGIC, hdr.magic, sizeof(CPIO_NEWC_MAGIC)-1))
	return CPIOERR_BAD_MAGIC;

    GET_NUM_FIELD(hdr.inode, st->st_ino);
    GET_NUM_FIELD(hdr.mode, st->st_mode);
    GET_NUM_FIELD(hdr.uid, st->st_uid);
    GET_NUM_FIELD(hdr.gid, st->st_gid);
    GET_NUM_FIELD(hdr.nlink, st->st_nlink);
    GET_NUM_FIELD(hdr.mtime, st->st_mtime);
    GET_NUM_FIELD(hdr.filesize, st->st_size);

    GET_NUM_FIELD(hdr.devMajor, major);
    GET_NUM_FIELD(hdr.devMinor, minor);
    st->st_dev = makedev(major, minor);

    GET_NUM_FIELD(hdr.rdevMajor, major);
    GET_NUM_FIELD(hdr.rdevMinor, minor);
    st->st_rdev = makedev(major, minor);

    GET_NUM_FIELD(hdr.namesize, nameSize);
    if (nameSize >= fsm->wrsize)
	return CPIOERR_BAD_HEADER;

    fsm->wrlen = nameSize;
    rc = fsmNext(fsm, FSM_DREAD);
    if (!rc && fsm->rdnb != fsm->wrlen)
	rc = CPIOERR_BAD_HEADER;

    if (!rc) {
	path = xmalloc(nameSize + 1);
	memcpy(path, fsm->wrbuf, fsm->rdnb);
	path[nameSize] = '\0';
    }
    fsm->path = path;
    return rc;
}

const char * cpioStrerror(int rc)
{
    static char msg[256];
    const char *s;
    int myerrno = errno;
    size_t l;

    strcpy(msg, "cpio: ");
    switch (rc) {
    default: {
	char *t = msg + strlen(msg);
	sprintf(t, _("(error 0x%x)"), (unsigned)rc);
	s = NULL;
	break;
    }
    case CPIOERR_BAD_MAGIC:	s = _("Bad magic");		break;
    case CPIOERR_BAD_HEADER:	s = _("Bad/unreadable  header");break;

    case CPIOERR_OPEN_FAILED:	s = "open";	break;
    case CPIOERR_CHMOD_FAILED:	s = "chmod";	break;
    case CPIOERR_CHOWN_FAILED:	s = "chown";	break;
    case CPIOERR_WRITE_FAILED:	s = "write";	break;
    case CPIOERR_UTIME_FAILED:	s = "utime";	break;
    case CPIOERR_UNLINK_FAILED:	s = "unlink";	break;
    case CPIOERR_RENAME_FAILED:	s = "rename";	break;
    case CPIOERR_SYMLINK_FAILED: s = "symlink";	break;
    case CPIOERR_STAT_FAILED:	s = "stat";	break;
    case CPIOERR_LSTAT_FAILED:	s = "lstat";	break;
    case CPIOERR_MKDIR_FAILED:	s = "mkdir";	break;
    case CPIOERR_RMDIR_FAILED:	s = "rmdir";	break;
    case CPIOERR_MKNOD_FAILED:	s = "mknod";	break;
    case CPIOERR_MKFIFO_FAILED:	s = "mkfifo";	break;
    case CPIOERR_LINK_FAILED:	s = "link";	break;
    case CPIOERR_READLINK_FAILED: s = "readlink";	break;
    case CPIOERR_READ_FAILED:	s = "read";	break;
    case CPIOERR_COPY_FAILED:	s = "copy";	break;
    case CPIOERR_LSETFCON_FAILED: s = "lsetfilecon";	break;
    case CPIOERR_SETCAP_FAILED: s = "cap_set_file";	break;

    case CPIOERR_HDR_SIZE:	s = _("Header size too big");	break;
    case CPIOERR_UNKNOWN_FILETYPE: s = _("Unknown file type");	break;
    case CPIOERR_MISSING_HARDLINK: s = _("Missing hard link(s)"); break;
    case CPIOERR_DIGEST_MISMATCH: s = _("Digest mismatch");	break;
    case CPIOERR_INTERNAL:	s = _("Internal error");	break;
    case CPIOERR_UNMAPPED_FILE:	s = _("Archive file not in header"); break;
    case CPIOERR_ENOENT:	s = strerror(ENOENT); break;
    case CPIOERR_ENOTEMPTY:	s = strerror(ENOTEMPTY); break;
    }

    l = sizeof(msg) - strlen(msg) - 1;
    if (s != NULL) {
	if (l > 0) strncat(msg, s, l);
	l -= strlen(s);
    }
    if ((rc & CPIOERR_CHECK_ERRNO) && myerrno) {
	s = _(" failed - ");
	if (l > 0) strncat(msg, s, l);
	l -= strlen(s);
	if (l > 0) strncat(msg, strerror(myerrno), l);
    }
    return msg;
}
