#include "fs.h"
#include "defs.h"
#include "mm.h"
#include "slub.h"
#include "task_manager.h"
#include "virtio.h"
#include "string.h"

#define BLOCK_SIZE 4096
#define SFS_NENTRY (BLOCK_SIZE / sizeof(struct sfs_entry))
#define min(a, b) ((a) < (b) ? (a) : (b))

struct sfs_fs sfs;
bool fs_initialized = 0;

void disk_op(int blockno, uint8_t *data, bool write) {
    struct buf b = {.disk = 0, .blockno = blockno, .data = (uint8_t *)PHYSICAL_ADDR(data)};
    virtio_disk_rw((struct buf *)PHYSICAL_ADDR(&b), write);
}

#define disk_read(blockno, data) disk_op(blockno, data, 0)
#define disk_write(blockno, data) disk_op(blockno, data, 1)
struct sfs_memory_block* sfs_get_block(uint32_t blockno) {
    struct list_head *pos;
    struct sfs_memory_block *mb;

    // 查找缓存
    list_for_each(pos, &sfs.inode_list) {
        mb = list_entry(pos, struct sfs_memory_block, inode_link);
        if (mb->blockno == blockno) {
            mb->reclaim_count++;
            return mb;
        }
    }

    // 创建新缓存块
    mb = (struct sfs_memory_block*)kmalloc(sizeof(struct sfs_memory_block));
    mb->block.block = (char*)kmalloc(BLOCK_SIZE);
    disk_read(blockno, (uint8_t*)mb->block.block);
    
    mb->blockno = blockno;
    mb->dirty = 0;
    mb->reclaim_count = 1;
    
    // 判断是否为 inode 块
    struct sfs_inode *test = (struct sfs_inode*)mb->block.block;
    mb->is_inode = (blockno == 1 || (blockno >= 3 && 
                    (test->type == SFS_FILE || test->type == SFS_DIRECTORY)));
    if (mb->is_inode) mb->block.din = test;
    
    list_add(&mb->inode_link, &sfs.inode_list);
    return mb;
}

void sfs_put_block(struct sfs_memory_block* mb) {
    if (mb->reclaim_count > 0) mb->reclaim_count--;
}

void sfs_mark_inode_dirty(struct sfs_inode* inode) {
    struct list_head *pos;
    list_for_each(pos, &sfs.inode_list) {
        struct sfs_memory_block *mb = list_entry(pos, struct sfs_memory_block, inode_link);
        if (mb->is_inode && (struct sfs_inode*)mb->block.block == inode) {
            mb->dirty = 1;
            return;
        }
    }
}

void sfs_sync() {
    struct list_head *pos, *n;
    list_for_each_safe(pos, n, &sfs.inode_list) {
        struct sfs_memory_block *mb = list_entry(pos, struct sfs_memory_block, inode_link);
        if (mb->dirty) {
            disk_write(mb->blockno, (uint8_t*)mb->block.block);
            mb->dirty = 0;
        }
        if (mb->reclaim_count == 0) {
            list_del(&mb->inode_link);
            kfree(mb->block.block);
            kfree(mb);
        }
    }
    if (sfs.super_dirty) {
        disk_write(0, (uint8_t*)&sfs.super);
        disk_write(2, sfs.freemap->map);
        sfs.super_dirty = 0;
    }
}

int sfs_alloc_block() {
    uint32_t byte_len = sfs.super.blocks / 8;
    for (int i = 0; i < byte_len; i++) {
        if (sfs.freemap->map[i] != 0xFF) {
            for (int j = 0; j < 8; j++) {
                if (!((sfs.freemap->map[i] >> j) & 1)) {
                    sfs.freemap->map[i] |= (1 << j);
                    sfs.super.unused_blocks--;
                    sfs.super_dirty = 1;
                    return i * 8 + j;
                }
            }
        }
    }
    return -1;
}
int sfs_create_entry(uint32_t parent_ino, const char* filename, uint16_t type) {
    int new_ino = sfs_alloc_block();
    if (new_ino == -1) return -1;

    struct sfs_memory_block *mb_new = sfs_get_block(new_ino);
    mb_new->is_inode = 1;
    struct sfs_inode *new_inode = (struct sfs_inode*)mb_new->block.block;
    memset(new_inode, 0, sizeof(struct sfs_inode));
    new_inode->type = type;
    new_inode->links = 1;
    
    // 如果是目录，创建 . 和 .. 条目
    if (type == SFS_DIRECTORY) {
        int dir_blk = sfs_alloc_block();
        if (dir_blk != -1) {
            new_inode->blocks = 1;
            new_inode->direct[0] = dir_blk;
            new_inode->size = 2 * sizeof(struct sfs_entry);
            
            struct sfs_memory_block *mb_dir = sfs_get_block(dir_blk);
            struct sfs_entry *entries = (struct sfs_entry*)mb_dir->block.block;
            memset(entries, 0, BLOCK_SIZE);
            
            // 正确设置 . 和 .. 条目
            entries[0].ino = new_ino;
            strcpy(entries[0].filename, ".");
            
            entries[1].ino = parent_ino;
            strcpy(entries[1].filename, "..");
            
            mb_dir->dirty = 1;
            sfs_put_block(mb_dir);
        }
    }
    mb_new->dirty = 1;
    sfs_put_block(mb_new);

    // 添加到父目录
    struct sfs_memory_block *mb_parent = sfs_get_block(parent_ino);
    mb_parent->is_inode = 1;
    struct sfs_inode *parent = (struct sfs_inode*)mb_parent->block.block;
    
    for (int i = 0; i < SFS_NDIRECT; i++) {
        // 分配新块如果需要
        if (i >= parent->blocks) {
            int new_blk = sfs_alloc_block();
            if (new_blk == -1) break;
            parent->direct[i] = new_blk;
            parent->blocks++;
            struct sfs_memory_block *tmp = sfs_get_block(new_blk);
            memset(tmp->block.block, 0, BLOCK_SIZE);
            tmp->dirty = 1;
            sfs_put_block(tmp);
        }

        // 查找空位
        struct sfs_memory_block *mb_dir = sfs_get_block(parent->direct[i]);
        struct sfs_entry *entries = (struct sfs_entry*)mb_dir->block.block;
        for (int j = 0; j < SFS_NENTRY; j++) {
            if (entries[j].ino == 0) {
                entries[j].ino = new_ino;
                strcpy(entries[j].filename, filename);
                parent->size += sizeof(struct sfs_entry);
                mb_dir->dirty = 1;
                mb_parent->dirty = 1;
                sfs_put_block(mb_dir);
                sfs_put_block(mb_parent);
                return new_ino;
            }
        }
        sfs_put_block(mb_dir);
    }
    
    sfs_put_block(mb_parent);
    return -1;
}

int sfs_lookup(const char* path) {
    if (!fs_initialized) sfs_init();
    if (path[0] != '/') return -1;
    
    uint32_t current_ino = 1;
    const char *p = path + 1;
    
    if (*p == '\0') return current_ino;  // 根目录
    
    struct sfs_memory_block *mb_cur = sfs_get_block(current_ino);
    mb_cur->is_inode = 1;
    struct sfs_inode *inode_cur = (struct sfs_inode*)mb_cur->block.block;
    
    char name[SFS_MAX_FILENAME_LEN + 1];
    
    while (*p) {
        int i = 0;
        while (*p && *p != '/' && i < SFS_MAX_FILENAME_LEN) name[i++] = *p++;
        name[i] = '\0';
        if (*p == '/') p++;
        
        if (inode_cur->type != SFS_DIRECTORY) {
            sfs_put_block(mb_cur);
            return -1;
        }
        
        // 查找条目
        uint32_t next_ino = 0;
        bool found = 0;
        
        for (int k = 0; k < inode_cur->blocks && k < SFS_NDIRECT; k++) {
            struct sfs_memory_block *mb_dir = sfs_get_block(inode_cur->direct[k]);
            struct sfs_entry *entries = (struct sfs_entry*)mb_dir->block.block;
            
            for (int j = 0; j < SFS_NENTRY; j++) {
                if (entries[j].ino && strcmp(entries[j].filename, name) == 0) {
                    next_ino = entries[j].ino;
                    found = 1;
                    break;
                }
            }
            sfs_put_block(mb_dir);
            if (found) break;
        }
        
        if (!found) {
            sfs_put_block(mb_cur);
            return -1;
        }
        
        sfs_put_block(mb_cur);
        current_ino = next_ino;
        mb_cur = sfs_get_block(current_ino);
        mb_cur->is_inode = 1;
        inode_cur = (struct sfs_inode*)mb_cur->block.block;
    }
    
    sfs_put_block(mb_cur);
    return current_ino;
}

// 根据 inode 号获取目录内容
int sfs_get_dir_entries(uint32_t dir_ino, char* files[]) {
    if (!fs_initialized) sfs_init();
    
    struct sfs_memory_block *mb = sfs_get_block(dir_ino);
    mb->is_inode = 1;
    struct sfs_inode *dir = (struct sfs_inode*)mb->block.block;
    
    if (dir->type != SFS_DIRECTORY) {
        sfs_put_block(mb);
        return -1;
    }
    
    int count = 0;
    for (int i = 0; i < dir->blocks; i++) {
        struct sfs_memory_block *data_mb = sfs_get_block(dir->direct[i]);
        struct sfs_entry *entries = (struct sfs_entry*)data_mb->block.block;
        for (int j = 0; j < SFS_NENTRY; j++) {
            if (entries[j].ino) {
                strcpy(files[count++], entries[j].filename);
            }
        }
        sfs_put_block(data_mb);
    }
    
    sfs_put_block(mb);
    return count;
}


int sfs_init() {
    if (fs_initialized) return 0;
    disk_read(0, (uint8_t*)&sfs.super);
    sfs.freemap = (struct bitmap*)kmalloc(sizeof(struct bitmap));
    sfs.freemap->size = sfs.super.blocks;
    sfs.freemap->map = (uint8_t*)kmalloc(BLOCK_SIZE);
    disk_read(2, sfs.freemap->map);
    sfs.super_dirty = 0;
    INIT_LIST_HEAD(&sfs.inode_list);
    fs_initialized = 1;
    return 0;
}

int sfs_open(const char* path, uint32_t flags) {
    if (!fs_initialized) sfs_init();
    if (path[0] != '/') return -1;

    uint32_t current_ino = 1;
    struct sfs_memory_block *mb_cur = sfs_get_block(current_ino);
    mb_cur->is_inode = 1;
    struct sfs_inode *inode_cur = (struct sfs_inode*)mb_cur->block.block;

    const char *p = path + 1;
    char name[SFS_MAX_FILENAME_LEN + 1];
    
    // 如果是根目录
    if (*p == '\0') {
        for (int i = 0; i < 16; i++) {
            if (!current->fs.fds[i]) {
                struct file *f = (struct file*)kmalloc(sizeof(struct file));
                f->inode = inode_cur;
                f->flags = flags;
                f->off = 0;
                current->fs.fds[i] = f;
                return i;
            }
        }
        sfs_put_block(mb_cur);
        return -1;
    }
    
    // 解析路径
    while (*p) {
        int i = 0;
        while (*p && *p != '/' && i < SFS_MAX_FILENAME_LEN) name[i++] = *p++;
        name[i] = '\0';
        
        bool is_last = (*p == '\0' || (*p == '/' && *(p+1) == '\0'));
        if (*p == '/') p++;

        if (inode_cur->type != SFS_DIRECTORY) {
            sfs_put_block(mb_cur);
            return -1;
        }

        // 查找条目
        uint32_t next_ino = 0;
        bool found = 0;
        for (int k = 0; k < inode_cur->blocks && k < SFS_NDIRECT; k++) {
            struct sfs_memory_block *mb_dir = sfs_get_block(inode_cur->direct[k]);
            struct sfs_entry *entries = (struct sfs_entry*)mb_dir->block.block;
            for (int j = 0; j < SFS_NENTRY; j++) {
                if (entries[j].ino && strcmp(entries[j].filename, name) == 0) {
                    next_ino = entries[j].ino;
                    found = 1;
                    break;
                }
            }
            sfs_put_block(mb_dir);
            if (found) break;
        }

        if (!found) {
            // 创建新条目
            if (!(flags & SFS_FLAG_WRITE)) {
                sfs_put_block(mb_cur);
                return -1;
            }
            uint32_t parent_ino = current_ino;
            sfs_put_block(mb_cur);
            
            int new_ino = sfs_create_entry(parent_ino, name, is_last ? SFS_FILE : SFS_DIRECTORY);
            if (new_ino == -1) return -1;
            
            current_ino = new_ino;
            mb_cur = sfs_get_block(current_ino);
            mb_cur->is_inode = 1;
            inode_cur = (struct sfs_inode*)mb_cur->block.block;
        } else {
            sfs_put_block(mb_cur);
            current_ino = next_ino;
            mb_cur = sfs_get_block(current_ino);
            mb_cur->is_inode = 1;
            inode_cur = (struct sfs_inode*)mb_cur->block.block;
        }
    }

    // 分配文件描述符
    for (int i = 0; i < 16; i++) {
        if (!current->fs.fds[i]) {
            struct file *f = (struct file*)kmalloc(sizeof(struct file));
            f->inode = inode_cur;
            f->flags = flags;
            f->off = 0;
            current->fs.fds[i] = f;
            return i;
        }
    }
    
    sfs_put_block(mb_cur);
    return -1;
}

int sfs_close(int fd) {
    struct file *f = current->fs.fds[fd];
    if (!f) return -1;

    // 释放 inode 引用
    struct list_head *pos;
    list_for_each(pos, &sfs.inode_list) {
        struct sfs_memory_block *mb = list_entry(pos, struct sfs_memory_block, inode_link);
        if (mb->is_inode && (struct sfs_inode*)mb->block.block == f->inode) {
            sfs_put_block(mb);
            break;
        }
    }

    sfs_sync();
    kfree(f);
    current->fs.fds[fd] = NULL;
    return 0;
}

int sfs_read(int fd, char* buf, uint32_t len) {
    struct file *f = current->fs.fds[fd];
    if (!f || f->inode->type == SFS_DIRECTORY) return -1;

    len = min(len, f->inode->size - f->off);
    uint32_t read_bytes = 0;
    
    while (read_bytes < len) {
        uint32_t blk_idx = (f->off + read_bytes) / BLOCK_SIZE;
        if (blk_idx >= SFS_NDIRECT || !f->inode->direct[blk_idx]) break;
        
        uint32_t offset = (f->off + read_bytes) % BLOCK_SIZE;
        uint32_t to_read = min(len - read_bytes, BLOCK_SIZE - offset);
        
        struct sfs_memory_block *mb = sfs_get_block(f->inode->direct[blk_idx]);
        memcpy(buf + read_bytes, mb->block.block + offset, to_read);
        sfs_put_block(mb);
        
        read_bytes += to_read;
    }

    f->off += read_bytes;
    return read_bytes;
}

int sfs_write(int fd, char* buf, uint32_t len) {
    struct file *f = current->fs.fds[fd];
    if (!f || !(f->flags & SFS_FLAG_WRITE)) return -1;

    uint32_t written = 0;
    while (written < len) {
        uint32_t blk_idx = (f->off + written) / BLOCK_SIZE;
        if (blk_idx >= SFS_NDIRECT) break;

        // 分配新块
        if (blk_idx >= f->inode->blocks) {
            int new_blk = sfs_alloc_block();
            if (new_blk == -1) break;
            f->inode->direct[blk_idx] = new_blk;
            f->inode->blocks++;
            sfs_mark_inode_dirty(f->inode);
        }

        uint32_t offset = (f->off + written) % BLOCK_SIZE;
        uint32_t to_write = min(len - written, BLOCK_SIZE - offset);
        
        struct sfs_memory_block *mb = sfs_get_block(f->inode->direct[blk_idx]);
        memcpy(mb->block.block + offset, buf + written, to_write);
        mb->dirty = 1;
        sfs_put_block(mb);
        
        written += to_write;
    }

    f->off += written;
    if (f->off > f->inode->size) {
        f->inode->size = f->off;
        sfs_mark_inode_dirty(f->inode);
    }
    
    return written;
}

int sfs_seek(int fd, int32_t off, int fromwhere) {
    struct file *f = current->fs.fds[fd];
    if (!f) return -1;
    
    int32_t new_off = (fromwhere == SEEK_SET) ? off :
                      (fromwhere == SEEK_CUR) ? f->off + off :
                      f->inode->size + off;
    
    if (new_off < 0 || new_off > f->inode->size) return -1;
    f->off = new_off;
    return 0;
}

int sfs_get_files(const char* path, char* files[]) {
    int ino = sfs_lookup(path);
    if (ino == -1) return -1;
    return sfs_get_dir_entries(ino, files);
}