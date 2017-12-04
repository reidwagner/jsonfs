/*
TODO:
Add release for deleting files
Should track size of files instead of using strlen. Binary files won't have \0
Handle dates
Handle permissions
 */

#define FUSE_USE_VERSION 32
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <unistd.h>
#include <cjson/cJSON.h>

#define d_data(x) cJSON_by_key(x, "data")
#define d_type(x) cJSON_by_key(x, "type")

static char *root_json;
static char* fs_image;
cJSON *root;

static cJSON* cJSON_by_key(cJSON *parent, const char *key)
{
    cJSON *dentry = parent->child;
    while (dentry) {
        if (strcmp(dentry->string, key) == 0)
            return dentry;
        dentry = dentry->next;
    }
    return NULL;
}

/* Should use cJSON library for searching */
static cJSON *cJSON_by_path(cJSON *parent, const char *path)
{
    char *next_parent_string;
    cJSON *next_parent, *parent_data = d_data(parent);
    if (strcmp("/", path) == 0)
        return root;

    for (int i = 1; path[i]; i++) {
        if (path[i] == '/') {
            /* -2 for the / on each end, +1 for the null terminator */
            next_parent_string = calloc(i - 1, sizeof(char));
            memcpy(next_parent_string, path + 1, i - 1);
            next_parent = cJSON_by_key(parent_data, next_parent_string);
            return cJSON_by_path(next_parent, path + i);
        }
    }
    return cJSON_by_key(parent_data, path + 1);
}

static void flush_fs_image() {
    FILE *f = fopen(fs_image, "w");
    fprintf(f, "%s", cJSON_Print(root));
    fclose(f);
}

static void read_fs_image(char *name) {
    int chunk = 1 << 10, nc = 0, c;
    int length = chunk;
    FILE *f = fopen(fs_image, "r");
    if (f) {
        root_json = calloc(chunk, sizeof(char));

        while ((c = fgetc(f)) != EOF) {
            if (nc*sizeof(char) > length - 1) {
                length += chunk;
                root_json = realloc(root_json, length);
            }
            root_json[nc++] = c;
        }
        fclose(f);
        root = cJSON_Parse(root_json);
    } else if (errno == ENOENT) {
        root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "type", "directory");
        cJSON_AddItemToObject(root, "data", cJSON_CreateObject());
        flush_fs_image();
    } else {
        perror("Error opening jsonfs image.\n");
        exit(1);
    }
}

static int split_path(const char *path, char *dirname_str, char *basename_str)
{
        char *path_copy = malloc(strlen(path) * sizeof(char));
        strcpy(path_copy, path);

        /* dirname and basename "may return pointers to statically allocated memory which may be overwritten by subsequent calls" */
        char *dirname_str_tmp;
        dirname_str_tmp = dirname(path_copy);
        if (!dirname_str_tmp)
            return -EINVAL;
        strcpy(dirname_str, dirname_str_tmp);

        /* dirname and basename "may modify the copy of path"*/
        strcpy(path_copy, path);

        char *basename_str_tmp;
        basename_str_tmp = basename(path_copy);
        if (!basename_str_tmp)
            return -EINVAL;
        strcpy(basename_str, basename_str_tmp);

        free(path_copy);
        return 0;
}

static int jsonfs_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi)
{
    int res = 0;
    char *jsonfs_type;
    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
    } else {
        memset(stbuf, 0, sizeof(struct stat));
        cJSON *dentry = cJSON_by_path(root, path);
        if (!dentry) {
            stbuf->st_mode = S_IFREG | 0666;
            return -ENOENT;
        }
        cJSON *dentry_data = d_data(dentry);
        jsonfs_type = d_type(dentry)->valuestring;
        if (strcmp(jsonfs_type, "directory") == 0) {
            stbuf->st_mode = S_IFDIR | 0755;
        } else {
            stbuf->st_mode = S_IFREG | 0666;
            if (dentry_data->valuestring)
                stbuf->st_size = strlen(dentry_data->valuestring);
            else
                stbuf->st_size = 0;
        }
    }
    return res;
}

static int jsonfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
        off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags)
{
    cJSON *dir, *dentries, *dentry;
    if (strcmp("/", path) == 0)
        dir = root;
    else
        dir = cJSON_by_path(root, path);
    if (!dir)
        return -ENOENT;
    dentries = d_data(dir);
    dentry = dentries->child;

    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);
    while (dentry) {
        filler(buf, dentry->string , NULL, 0, 0);
        dentry = dentry->next;
    }
    return 0;
}

static int jsonfs_open(const char *path, struct fuse_file_info *fi)
{
    if (!cJSON_by_path(root, path)) {
        char *dirname_str = malloc(4096 * sizeof(char));
        char *basename_str = malloc(255 * sizeof(char));
        int res;
        if ((res = split_path(path, dirname_str, basename_str)))
            return res;

        cJSON *newfile, *dir_data, *dir = cJSON_by_path(root, dirname_str);
        dir_data = d_data(dir);
        cJSON_AddItemToObject(dir_data, basename_str, newfile = cJSON_CreateObject());
        cJSON_AddStringToObject(newfile, "type", "regular");
        cJSON_AddStringToObject(newfile, "data", "");

        free(dirname_str);
        free(basename_str);

        flush_fs_image();
    }
    return 0;
}

static int jsonfs_read(const char *path, char *buf, size_t size, off_t offset,
        struct fuse_file_info *fi)
{
    size_t len;
    cJSON *dentry_data, *dentry = cJSON_by_path(root, path);
    if (!dentry)
        return -ENOENT;
    dentry_data = d_data(dentry);
    if (dentry_data->valuestring)
        len = strlen(dentry_data->valuestring);
    else
        return 0;
    if (offset < len) {
        if (offset + size > len)
            size = len - offset;
        memcpy(buf, dentry_data->valuestring + offset, size);
    } else
        size = 0;
    return size;
}

static int jsonfs_write(const char *path, const char *buf, size_t size, off_t offset,
        struct fuse_file_info *fi)
{
    cJSON *dentry_data, *dentry = cJSON_by_path(root, path);
    if (!dentry)
        return -ENOENT;
    dentry_data = d_data(dentry);
    dentry_data->valuestring = realloc(dentry_data->valuestring, offset + size + 1);
    memcpy(dentry_data->valuestring + offset, buf, size);
    dentry_data->valuestring[size] = '\0';
    flush_fs_image();
    return size;
}

static int jsonfs_truncate(const char *path, off_t offset, struct fuse_file_info *fi)
{
    cJSON *dentry_data, *dentry;
    dentry = cJSON_by_path(root, path);
    dentry_data = d_data(dentry);
    dentry_data->valuestring = realloc(dentry_data->valuestring, offset);
    if (offset)
        dentry_data->valuestring[offset - 1] = '\0';
    return 0;
}

static int jsonfs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    return jsonfs_open(path, fi);
}

static int jsonfs_mkdir(const char *path, mode_t mode)
{
    char *dirname_str = malloc(4096 * sizeof(char));
    char *basename_str = malloc(255 * sizeof(char));
    int res;
    if ((res = split_path(path, dirname_str, basename_str)))
        return res;

    cJSON *newdir, *dir_data, *dir = cJSON_by_path(root, dirname_str);
    dir_data = d_data(dir);
    cJSON_AddItemToObject(dir_data, basename_str, newdir = cJSON_CreateObject());
    cJSON_AddStringToObject(newdir, "type", "directory");
    cJSON_AddItemToObject(newdir, "data", cJSON_CreateObject());
    flush_fs_image();

    free(dirname_str);
    free(basename_str);

    return 0;
}

static struct fuse_operations jsonfs_ops = {
    .getattr        = jsonfs_getattr,
    .readdir        = jsonfs_readdir,
    .open           = jsonfs_open,
    .read           = jsonfs_read,
    .write          = jsonfs_write,
    .truncate       = jsonfs_truncate,
    .create         = jsonfs_create,
    .mkdir          = jsonfs_mkdir,
};

int main(int argc, char *argv[])
{
    struct fuse_args args = FUSE_ARGS_INIT(0, NULL);
    for (int i = 0; i < argc; i++)
        if(strncmp(argv[i], "-json=", 6) == 0) {
            fs_image = malloc((strlen(argv[i]) - 6) * sizeof(char));
            strcpy(fs_image, argv[i] + 6);
        } else {
            fuse_opt_add_arg(&args, argv[i]);
        }
    if (!fs_image) {
        perror("Error: The jsonfs image path must be supplied with -json=/full/path/to/image. It need not exist yet.\n");
        exit(1);
    }
    read_fs_image(fs_image);
    return fuse_main(args.argc, args.argv, &jsonfs_ops, NULL);
}
