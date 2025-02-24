#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/sched.h>
#include <asm/current.h>
#include <asm/uaccess.h>
#include <linux/fcntl.h>
#include <linux/poll.h>
#include <linux/time.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <net/net_namespace.h>
#include <linux/string.h>
#include "wmt_exp.h"
#include "stp_exp.h"

MODULE_LICENSE("Dual BSD/GPL");

#define WIFI_DRIVER_NAME "mtk_wmt_WIFI_chrdev"
#define WIFI_DEV_MAJOR 153

#define PFX                         "[MTK-WIFI] "
#define WIFI_LOG_DBG                  3
#define WIFI_LOG_INFO                 2
#define WIFI_LOG_WARN                 1
#define WIFI_LOG_ERR                  0


unsigned int gDbgLevel = WIFI_LOG_DBG;

#define WIFI_DBG_FUNC(fmt, arg...)    if(gDbgLevel >= WIFI_LOG_DBG){ printk(PFX "%s: "  fmt, __FUNCTION__ ,##arg);}
#define WIFI_INFO_FUNC(fmt, arg...)   if(gDbgLevel >= WIFI_LOG_INFO){ printk(PFX "%s: "  fmt, __FUNCTION__ ,##arg);}
#define WIFI_WARN_FUNC(fmt, arg...)   if(gDbgLevel >= WIFI_LOG_WARN){ printk(PFX "%s: "  fmt, __FUNCTION__ ,##arg);}
#define WIFI_ERR_FUNC(fmt, arg...)    if(gDbgLevel >= WIFI_LOG_ERR){ printk(PFX "%s: "   fmt, __FUNCTION__ ,##arg);}
#define WIFI_TRC_FUNC(f)              if(gDbgLevel >= WIFI_LOG_DBG){printk(PFX "<%s> <%d>\n", __FUNCTION__, __LINE__);}

#define VERSION "1.0"
#define WLAN_IFACE_NAME "wlan0"

enum {
    WLAN_MODE_HALT,
    WLAN_MODE_AP,
    WLAN_MODE_STA_P2P,
    WLAN_MODE_MAX
};
static int wlan_mode = WLAN_MODE_HALT;
static int powered = 0;

/*
 *  enable = 1, mode = 0  => init P2P network
 *  enable = 1, mode = 1  => init Soft AP network
 *  enable = 0            => uninit P2P/AP network
 */
typedef struct _PARAM_CUSTOM_P2P_SET_STRUC_T {
    unsigned int  u4Enable;
    unsigned int  u4Mode;
} PARAM_CUSTOM_P2P_SET_STRUC_T, *P_PARAM_CUSTOM_P2P_SET_STRUC_T;
typedef int (*set_p2p_mode)(struct net_device *netdev, PARAM_CUSTOM_P2P_SET_STRUC_T p2pmode);

set_p2p_mode pf_set_p2p_mode;
void register_set_p2p_mode_handler(set_p2p_mode handler) {
    WIFI_INFO_FUNC("(pid %d) register set p2p mode handler %p\n", current->pid, handler);
    pf_set_p2p_mode = handler;
}
EXPORT_SYMBOL(register_set_p2p_mode_handler);

/* For dynamical debug level setting */
/* copy of debug.h in wlan driver */
#define DBG_CLASS_ERROR         BIT(0)
#define DBG_CLASS_WARN          BIT(1)
#define DBG_CLASS_STATE         BIT(2)
#define DBG_CLASS_EVENT         BIT(3)
#define DBG_CLASS_TRACE         BIT(4)
#define DBG_CLASS_INFO          BIT(5)
#define DBG_CLASS_LOUD          BIT(6)
#define DBG_CLASS_TEMP          BIT(7)
#define DBG_CLASS_MASK          BITS(0,7)

typedef enum _ENUM_DBG_MODULE_T {
    DBG_INIT_IDX = 0,       /* For driver initial */
    DBG_HAL_IDX,            /* For HAL(HW) Layer */
    DBG_INTR_IDX,           /* For Interrupt */
    DBG_REQ_IDX,
    DBG_TX_IDX,
    DBG_RX_IDX,
    DBG_RFTEST_IDX,         /* For RF test mode*/
    DBG_EMU_IDX,            /* Developer specific */

    DBG_SW1_IDX,            /* Developer specific */
    DBG_SW2_IDX,            /* Developer specific */
    DBG_SW3_IDX,            /* Developer specific */
    DBG_SW4_IDX,            /* Developer specific */

    DBG_HEM_IDX,            /* HEM */
    DBG_AIS_IDX,            /* AIS */
    DBG_RLM_IDX,            /* RLM */
    DBG_MEM_IDX,            /* RLM */
    DBG_CNM_IDX,            /* CNM */
    DBG_RSN_IDX,            /* RSN */
    DBG_BSS_IDX,            /* BSS */
    DBG_SCN_IDX,            /* SCN */
    DBG_SAA_IDX,            /* SAA */
    DBG_AAA_IDX,            /* AAA */
    DBG_P2P_IDX,            /* P2P */
    DBG_QM_IDX,             /* QUE_MGT */
    DBG_SEC_IDX,            /* SEC */
    DBG_BOW_IDX,            /* BOW */
    DBG_WAPI_IDX,           /* WAPI */
    DBG_ROAMING_IDX,        /* ROAMING */

    DBG_MODULE_NUM          /* Notice the XLOG check */
} ENUM_DBG_MODULE_T;
/*end -- copy of debug.h in wlan driver*/
typedef void (*set_dbg_level)(unsigned char modules[DBG_MODULE_NUM]);

unsigned char wlan_dbg_level[DBG_MODULE_NUM];
set_dbg_level pf_set_dbg_level;
void register_set_dbg_level_handler(set_dbg_level handler) {
	pf_set_dbg_level = handler;
}
EXPORT_SYMBOL(register_set_dbg_level_handler);
static int WIFI_devs = 1;        /* device count */
static int WIFI_major = WIFI_DEV_MAJOR;       /* dynamic allocation */
module_param(WIFI_major, uint, 0);
static struct cdev WIFI_cdev;
volatile int retflag = 0;
static struct semaphore wr_mtx;

/*******************************************************************
 *  WHOLE CHIP RESET PROCEDURE:
 *
 *  WMTRSTMSG_RESET_START callback
 *  -> wlanRemove
 *  -> WMTRSTMSG_RESET_END callback
 *
 *******************************************************************
*/
/*-----------------------------------------------------------------*/
/*
 *  Receiving RESET_START message
 */
/*-----------------------------------------------------------------*/
int wifi_reset_start(void)
{
    struct net_device *netdev = NULL;
    PARAM_CUSTOM_P2P_SET_STRUC_T p2pmode;

    down(&wr_mtx);

    if (powered == 1) {
        netdev = dev_get_by_name(&init_net, WLAN_IFACE_NAME);
        if (netdev == NULL) {
            WIFI_ERR_FUNC("Fail to get wlan0 net device\n");
        }
        else {
            p2pmode.u4Enable = 0;
            p2pmode.u4Mode = 0;

            if (pf_set_p2p_mode) {
                if (pf_set_p2p_mode(netdev, p2pmode) != 0){
                    WIFI_ERR_FUNC("Turn off p2p/ap mode fail");
                } else {
                    WIFI_INFO_FUNC("Turn off p2p/ap mode");
                }
            }
            dev_put(netdev);
            netdev = NULL;
        }
    }
    else {
        /* WIFI is off before whole chip reset, do nothing */
    }

    return 0;
}
EXPORT_SYMBOL(wifi_reset_start);

/*-----------------------------------------------------------------*/
/*
 *  Receiving RESET_END message
 */
/*-----------------------------------------------------------------*/
int wifi_reset_end(void)
{
    struct net_device *netdev = NULL;
    PARAM_CUSTOM_P2P_SET_STRUC_T p2pmode;
    int wait_cnt = 0;
    int ret = -1;

    WIFI_WARN_FUNC("WIFI state recovering...\n");

    if (powered == 1) {
        /* WIFI is on before whole chip reset, reopen it now */
        if (MTK_WCN_BOOL_FALSE == mtk_wcn_wmt_func_on(WMTDRV_TYPE_WIFI)) {
            WIFI_ERR_FUNC("WMT turn on WIFI fail!\n");
            goto done;
        }
        else {
            WIFI_INFO_FUNC("WMT turn on WIFI success!\n");
        }

        if (pf_set_p2p_mode == NULL) {
            WIFI_ERR_FUNC("Set p2p mode handler is NULL\n");
            goto done;
        }

        netdev = dev_get_by_name(&init_net, WLAN_IFACE_NAME);
        while (netdev == NULL && wait_cnt < 10) {
            WIFI_ERR_FUNC("Fail to get wlan0 net device, sleep 300ms\n");
            msleep(300);
            wait_cnt ++;
            netdev = dev_get_by_name(&init_net, WLAN_IFACE_NAME);
        }
        if (wait_cnt >= 10) {
            WIFI_ERR_FUNC("Get wlan0 net device timeout\n");
            goto done;
        }

        if (wlan_mode == WLAN_MODE_STA_P2P){
            p2pmode.u4Enable = 1;
            p2pmode.u4Mode = 0;
            if (pf_set_p2p_mode(netdev, p2pmode) != 0){
                WIFI_ERR_FUNC("Set wlan mode fail\n");
            }
            else{
                WIFI_WARN_FUNC("Set wlan mode %d\n", WLAN_MODE_STA_P2P);
                ret = 0;
            }
        } else if (wlan_mode == WLAN_MODE_AP){
            p2pmode.u4Enable = 1;
            p2pmode.u4Mode = 1;
            if (pf_set_p2p_mode(netdev, p2pmode) != 0){
                WIFI_ERR_FUNC("Set wlan mode fail\n");
            }
            else{
                WIFI_WARN_FUNC("Set wlan mode %d\n", WLAN_MODE_AP);
                ret = 0;
            }
        }
done:
        if (netdev != NULL){
            dev_put(netdev);
        }
    }
    else{
        /* WIFI is off before whole chip reset, do nothing */
        ret = 0;
    }
    up(&wr_mtx);
    return ret;
}
EXPORT_SYMBOL(wifi_reset_end);
static int WIFI_open(struct inode *inode, struct file *file)
{
    WIFI_INFO_FUNC("%s: major %d minor %d (pid %d)\n", __func__,
        imajor(inode),
        iminor(inode),
        current->pid
        );

    return 0;
}

static int WIFI_close(struct inode *inode, struct file *file)
{
    WIFI_INFO_FUNC("%s: major %d minor %d (pid %d)\n", __func__,
        imajor(inode),
        iminor(inode),
        current->pid
        );
    retflag = 0;

    return 0;
}

ssize_t WIFI_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
    int retval = -EIO;
    char local[12] = {0};
    struct net_device *netdev = NULL;
    PARAM_CUSTOM_P2P_SET_STRUC_T p2pmode;
    int wait_cnt = 0;

    down(&wr_mtx);
    if (count <= 0) {
        WIFI_ERR_FUNC("WIFI_write invalid param\n");
        goto done;
    }

    if (0 == copy_from_user(local, buf, (count > sizeof(local)) ? sizeof(local) : count)) {
        local[11] = 0;
        WIFI_INFO_FUNC("WIFI_write %s\n", local);

        if (local[0] == '0') {
                //TODO
                //Configure the EINT pin to GPIO mode.
            if (powered == 0) {
                WIFI_INFO_FUNC("WIFI is already power off!\n");
                retval = count;
                wlan_mode = WLAN_MODE_HALT;
                goto done;
            }

            netdev = dev_get_by_name(&init_net, WLAN_IFACE_NAME);
            if (netdev == NULL) {
                WIFI_ERR_FUNC("Fail to get wlan0 net device\n");
            }
            else {
                p2pmode.u4Enable = 0;
                p2pmode.u4Mode = 0;

                if (pf_set_p2p_mode) {
                    if (pf_set_p2p_mode(netdev, p2pmode) != 0){
                        WIFI_ERR_FUNC("Turn off p2p/ap mode fail");
                    } else {
                        WIFI_INFO_FUNC("Turn off p2p/ap mode");
                        wlan_mode = WLAN_MODE_HALT;
                    }
               }
               dev_put(netdev);
               netdev = NULL;
            }

                if (MTK_WCN_BOOL_FALSE == mtk_wcn_wmt_func_off(WMTDRV_TYPE_WIFI)) {
                WIFI_ERR_FUNC("WMT turn off WIFI fail!\n");
                }
                else {
                    WIFI_INFO_FUNC("WMT turn off WIFI OK!\n");
                powered = 0;
                    retval = count;
                wlan_mode = WLAN_MODE_HALT;
                }
            }
            else if (local[0] == '1') {
                //TODO
                //Disable EINT(external interrupt), and set the GPIO to EINT mode.
            if (powered == 1) {
                WIFI_INFO_FUNC("WIFI is already power on!\n");
                retval = count;
                goto done;
            }

                if (MTK_WCN_BOOL_FALSE == mtk_wcn_wmt_func_on(WMTDRV_TYPE_WIFI)) {
                WIFI_ERR_FUNC("WMT turn on WIFI fail!\n");
                }
                else {
                powered = 1;
                    retval = count;
                    WIFI_INFO_FUNC("WMT turn on WIFI success!\n");
                wlan_mode = WLAN_MODE_HALT;
            }
        }
        else if (local[0] == 'D') {
            int k = 0;
            /* 
             * 0: no debug
             * 1: common debug output
             * 2: more detials
             * 3: verbose
             */
            switch (local[1]) {
            case '0':
                for (k = 0; k < DBG_MODULE_NUM; k++) {
                    wlan_dbg_level[k] = 0;
                }
                if (pf_set_dbg_level) {
                    pf_set_dbg_level(wlan_dbg_level);
                }
                break;
            case '1':
                for (k = 0; k < DBG_MODULE_NUM; k++) {
                    wlan_dbg_level[k] = DBG_CLASS_ERROR | \
                        DBG_CLASS_WARN | \
                        DBG_CLASS_STATE | \
                        DBG_CLASS_EVENT | \
                        DBG_CLASS_TRACE | \
                        DBG_CLASS_INFO;
                }
                wlan_dbg_level[DBG_TX_IDX] &= ~(DBG_CLASS_EVENT | \
                    DBG_CLASS_TRACE | \
                    DBG_CLASS_INFO);
                wlan_dbg_level[DBG_RX_IDX] &= ~(DBG_CLASS_EVENT | \
                    DBG_CLASS_TRACE | \
                    DBG_CLASS_INFO);
                wlan_dbg_level[DBG_REQ_IDX] &= ~(DBG_CLASS_EVENT | \
                    DBG_CLASS_TRACE | \
                    DBG_CLASS_INFO);
                wlan_dbg_level[DBG_INTR_IDX] = 0;
                wlan_dbg_level[DBG_MEM_IDX] = 0;
                if (pf_set_dbg_level) {
                    pf_set_dbg_level(wlan_dbg_level);
                }
                break;
            case '2':
                for (k = 0; k < DBG_MODULE_NUM; k++) {
                    wlan_dbg_level[k] = DBG_CLASS_ERROR | \
                        DBG_CLASS_WARN | \
                        DBG_CLASS_STATE | \
                        DBG_CLASS_EVENT | \
                        DBG_CLASS_TRACE | \
                        DBG_CLASS_INFO;
                }	
                wlan_dbg_level[DBG_INTR_IDX] = 0;
                wlan_dbg_level[DBG_MEM_IDX] = 0;
                if (pf_set_dbg_level) {
                    pf_set_dbg_level(wlan_dbg_level);
                }
                break;
            case '3':
                for (k = 0; k < DBG_MODULE_NUM; k++) {
                    wlan_dbg_level[k] = DBG_CLASS_ERROR | \
                        DBG_CLASS_WARN | \
                        DBG_CLASS_STATE | \
                        DBG_CLASS_EVENT | \
                        DBG_CLASS_TRACE | \
                        DBG_CLASS_INFO | \
                        DBG_CLASS_LOUD;
                }	
                if (pf_set_dbg_level) {
                    pf_set_dbg_level(wlan_dbg_level);
                }
                break;
            default:
                break;
            }
        }
        else if (local[0] == 'S' || local[0] == 'P' || local[0] == 'A') {
            if (powered == 0) {
                /* If WIFI is off, turn on WIFI first */
                if (MTK_WCN_BOOL_FALSE == mtk_wcn_wmt_func_on(WMTDRV_TYPE_WIFI)) {
                    WIFI_ERR_FUNC("WMT turn on WIFI fail!\n");
                    goto done;
                }
                else {
                    powered = 1;
                    WIFI_INFO_FUNC("WMT turn on WIFI success!\n");
                    wlan_mode = WLAN_MODE_HALT;
                }
            }

            if (pf_set_p2p_mode == NULL) {
                WIFI_ERR_FUNC("Set p2p mode handler is NULL\n");
                goto done;
        }
            netdev = dev_get_by_name(&init_net, WLAN_IFACE_NAME);
            while (netdev == NULL && wait_cnt < 10) {
                WIFI_ERR_FUNC("Fail to get wlan0 net device, sleep 300ms\n");
                msleep(300);
                wait_cnt ++;
                netdev = dev_get_by_name(&init_net, WLAN_IFACE_NAME);
            }
            if (wait_cnt >= 10) {
                WIFI_ERR_FUNC("Get wlan0 net device timeout\n");
                goto done;
    }

            if ((wlan_mode == WLAN_MODE_AP && (local[0] == 'S' || local[0] == 'P')) ||
                (wlan_mode == WLAN_MODE_STA_P2P && (local[0] == 'A'))){
                    p2pmode.u4Enable = 0;
                    p2pmode.u4Mode = 0;
                    if (pf_set_p2p_mode(netdev, p2pmode) != 0){
                        WIFI_ERR_FUNC("Turn off p2p/ap mode fail");
                        goto done;
                    }
            }

            if (local[0] == 'S' || local[0] == 'P'){
                p2pmode.u4Enable = 1;
                p2pmode.u4Mode = 0;
                if (pf_set_p2p_mode(netdev, p2pmode) != 0){
                    WIFI_ERR_FUNC("Set wlan mode fail\n");
                }
                else{
                    WIFI_INFO_FUNC("Set wlan mode %d --> %d\n", wlan_mode, WLAN_MODE_STA_P2P);
                    wlan_mode = WLAN_MODE_STA_P2P;
                    retval = count;
                }
            } else if (local[0] == 'A'){
                p2pmode.u4Enable = 1;
                p2pmode.u4Mode = 1;
                if (pf_set_p2p_mode(netdev, p2pmode) != 0){
                    WIFI_ERR_FUNC("Set wlan mode fail\n");
                }
                else{
                    WIFI_INFO_FUNC("Set wlan mode %d --> %d\n", wlan_mode, WLAN_MODE_AP);
                    wlan_mode = WLAN_MODE_AP;
                    retval = count;
                }
            }
            dev_put(netdev);
            netdev = NULL;
        }
    }
done:
    if (netdev != NULL){
        dev_put(netdev);
    }
    up(&wr_mtx);
    return (retval);
}


struct file_operations WIFI_fops = {
    .open = WIFI_open,
    .release = WIFI_close,
    .write = WIFI_write,
};

static int WIFI_init(void)
{
    dev_t dev = MKDEV(WIFI_major, 0);
    int alloc_ret = 0;
    int cdev_err = 0;

    /*static allocate chrdev*/
    alloc_ret = register_chrdev_region(dev, 1, WIFI_DRIVER_NAME);
    if (alloc_ret) {
        WIFI_ERR_FUNC("fail to register chrdev\n");
        return alloc_ret;
    }

    cdev_init(&WIFI_cdev, &WIFI_fops);
    WIFI_cdev.owner = THIS_MODULE;

    cdev_err = cdev_add(&WIFI_cdev, dev, WIFI_devs);
    if (cdev_err) {
        goto error;
    }

    sema_init(&wr_mtx, 1);

    WIFI_INFO_FUNC("%s driver(major %d) installed.\n", WIFI_DRIVER_NAME, WIFI_major);
    retflag = 0;
    wlan_mode = WLAN_MODE_HALT;
    pf_set_p2p_mode = NULL;

    return 0;

error:
    if (cdev_err == 0) {
        cdev_del(&WIFI_cdev);
    }

    if (alloc_ret == 0) {
        unregister_chrdev_region(dev, WIFI_devs);
    }

    return -1;
}

static void WIFI_exit(void)
{
    dev_t dev = MKDEV(WIFI_major, 0);
    retflag = 0;

    cdev_del(&WIFI_cdev);
    unregister_chrdev_region(dev, WIFI_devs);

    WIFI_INFO_FUNC("%s driver removed.\n", WIFI_DRIVER_NAME);
}

#ifdef MTK_WCN_REMOVE_KERNEL_MODULE
int mtk_wcn_wmt_wifi_init(void)
{
    return WIFI_init();
}

void mtk_wcn_wmt_wifi_exit(void)
{
    return WIFI_exit();
}

EXPORT_SYMBOL(mtk_wcn_wmt_wifi_init);
EXPORT_SYMBOL(mtk_wcn_wmt_wifi_exit);
#else
module_init(WIFI_init);
module_exit(WIFI_exit);

#endif
