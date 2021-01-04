/** @file mlan_module.c
 *
 *  @brief This file declares the exported symbols from MLAN.
 *
 *
 *  Copyright 2014-2020 NXP
 *
 *  This software file (the File) is distributed by NXP
 *  under the terms of the GNU General Public License Version 2, June 1991
 *  (the License).  You may use, redistribute and/or modify the File in
 *  accordance with the terms and conditions of the License, a copy of which
 *  is available by writing to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 *  worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 *  THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 *  ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 *  this warranty disclaimer.
 *
 */

/******************************************************
Change log:
    12/08/2008: initial version
******************************************************/

#ifdef LINUX
#include <linux/module.h>
#include "mlan_decl.h"
#include "mlan_ioctl.h"

EXPORT_SYMBOL(mlan_register);
EXPORT_SYMBOL(mlan_unregister);
EXPORT_SYMBOL(mlan_init_fw);
EXPORT_SYMBOL(mlan_set_init_param);
EXPORT_SYMBOL(mlan_dnld_fw);
EXPORT_SYMBOL(mlan_shutdown_fw);
#ifdef USB
EXPORT_SYMBOL(mlan_write_data_async_complete);
EXPORT_SYMBOL(mlan_recv);
#endif
EXPORT_SYMBOL(mlan_send_packet);
EXPORT_SYMBOL(mlan_ioctl);
EXPORT_SYMBOL(mlan_main_process);
EXPORT_SYMBOL(mlan_rx_process);
EXPORT_SYMBOL(mlan_select_wmm_queue);
#if defined(SDIO) || defined(PCIE)
EXPORT_SYMBOL(mlan_interrupt);
#if defined(SYSKT)
EXPORT_SYMBOL(mlan_hs_callback);
#endif /* SYSKT_MULTI || SYSKT */
#endif /* SDIO || PCIE */

EXPORT_SYMBOL(mlan_pm_wakeup_card);
EXPORT_SYMBOL(mlan_is_main_process_running);
#ifdef PCIE
EXPORT_SYMBOL(mlan_set_int_mode);
#endif

MODULE_DESCRIPTION("M-WLAN MLAN Driver");
MODULE_AUTHOR("NXP");
MODULE_VERSION(MLAN_RELEASE_VERSION);
MODULE_LICENSE("GPL");
#endif /* LINUX */
