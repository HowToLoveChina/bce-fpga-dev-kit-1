/*
 * Copyright (C) 2017 Baidu, Inc. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/dmi.h>
#include <linux/acpi.h>
#include <acpi/acpi.h>

#include "xvc-mmconfig.h"

/* The physical address of the MMCONFIG aperture.  Set from ACPI tables. */
struct acpi_mcfg_allocation *pci_mmcfg_config;
int pci_mmcfg_config_num;

static int acpi_mcfg_check_entry(struct acpi_table_mcfg *mcfg, struct acpi_mcfg_allocation *cfg)
{
    int year;

    if (cfg->address < 0xFFFFFFFF)
        return 0;

    if (!strncmp(mcfg->header.oem_id, "SGI", 3))
        return 0;

    if (mcfg->header.revision >= 1) {
        if (dmi_get_date(DMI_BIOS_DATE, &year, NULL, NULL) && year >= 2010)
            return 0;
    }

    printk(KERN_ERR LOG_PREFIX "MCFG region for %04x:%02x-%02x at %#llx is above 4GB, ignored\n",
            cfg->pci_segment, cfg->start_bus_number, cfg->end_bus_number, cfg->address);
    return -EFAULT;
}

static int pci_parse_mcfg(struct acpi_table_header *header)
{
    struct acpi_table_mcfg *mcfg;
    unsigned long i;
    int config_size;

    if (!header)
        return -EINVAL;

    mcfg = (struct acpi_table_mcfg *)header;

    /* how many config structures do we have */
    pci_mmcfg_config_num = 0;
    i = header->length - sizeof(struct acpi_table_mcfg);
    while (i >= sizeof(struct acpi_mcfg_allocation)) {
        ++pci_mmcfg_config_num;
        i -= sizeof(struct acpi_mcfg_allocation);
    }
    if (pci_mmcfg_config_num == 0) {
        printk(KERN_ERR LOG_PREFIX "MMCONFIG has no entries\n");
        return -ENODEV;
    }

    config_size = pci_mmcfg_config_num * sizeof(*pci_mmcfg_config);
    pci_mmcfg_config = kmalloc(config_size, GFP_KERNEL);
    if (!pci_mmcfg_config) {
        printk(KERN_ERR LOG_PREFIX "No memory for MCFG config tables\n");
        return -ENOMEM;
    }

    memcpy(pci_mmcfg_config, &mcfg[1], config_size);

    for (i = 0; i < pci_mmcfg_config_num; ++i) {
        if (acpi_mcfg_check_entry(mcfg, &pci_mmcfg_config[i])) {
            kfree(pci_mmcfg_config);
            pci_mmcfg_config_num = 0;
            return -ENODEV;
        }
    }

    return 0;
}

int pci_mmcfg_driver_init(void)
{
    int result;
    struct acpi_table_header *mcfg_base;
    /*acpi_size mcfg_size;*/

    acpi_get_table(ACPI_SIG_MCFG, 0, &mcfg_base);
    /*acpi_get_table_with_size(ACPI_SIG_MCFG, 0, &mcfg_base, &mcfg_size);*/

    result = pci_parse_mcfg(mcfg_base);
    if (result != 0) {
        return result;
    }

    result = pci_mmcfg_arch_init();
    if (result == 1) {
        return 0;
    } else {
        return 1;
    }
}

void pci_mmcfg_driver_exit(void)
{
    pci_mmcfg_arch_free();
}

