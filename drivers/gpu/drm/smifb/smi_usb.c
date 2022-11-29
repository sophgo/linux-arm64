#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>
#include <linux/version.h>
#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>

#include "smi_drv.h"
#include "hw768.h"
#include "ddk768/ddk768_intr.h"
#include "ddk768/ddk768_power.h"


#ifdef CONFIG_CPU_LOONGSON3
#include "loongson-ehci.h"
#else
#include "ehci.h"
#endif


#define SMI_HOST_OFFSET 0x00170000

static DECLARE_RWSEM(companions_rwsem);


/* called after powerup, by probe or system-pm "wakeup" */
static int ehci_pci_reinit(struct ehci_hcd *ehci, struct pci_dev *pdev)
{
	int			retval;

	/* we expect static quirk code to handle the "extended capabilities"
	 * (currently just BIOS handoff) allowed starting with EHCI 0.96
	 */

	/* PCI Memory-Write-Invalidate cycle support is optional (uncommon) */
	retval = pci_set_mwi(pdev);
	if (!retval)
		ehci_dbg(ehci, "MWI active\n");


	return 0;
}

/* called during probe() after chip reset completes */
static int ehci_pci_setup(struct usb_hcd *hcd)
{
	struct ehci_hcd		*ehci = hcd_to_ehci(hcd);
	struct pci_dev		*pdev = to_pci_dev(hcd->self.controller);
	int			retval;

	ehci->caps = hcd->regs;

	retval = ehci_setup(hcd);
	if (retval)
		return retval;

	pci_read_config_byte(pdev, 0x60, &ehci->sbrn);

	/* Keep this around for a while just in case some EHCI
	 * implementation uses legacy PCI PM support.  This test
	 * can be removed on 17 Dec 2009 if the dev_warn() hasn't
	 * been triggered by then.
	 */
	if (!device_can_wakeup(&pdev->dev)) {
		u16	port_wake;

		pci_read_config_word(pdev, 0x62, &port_wake);
		if (port_wake & 0x0001) {
			dev_warn(&pdev->dev, "Enabling legacy PCI PM\n");
			device_set_wakeup_capable(&pdev->dev, 1);
		}
	}

#ifdef	CONFIG_PM
	if (ehci->no_selective_suspend && device_can_wakeup(&pdev->dev))
		ehci_warn(ehci, "selective suspend/wakeup unavailable\n");
#endif

	retval = ehci_pci_reinit(ehci, pdev);

	return retval;
}

/*-------------------------------------------------------------------------*/

#ifdef	CONFIG_PM

/* suspend/resume, section 4.3 */

/* These routines rely on the PCI bus glue
 * to handle powerdown and wakeup, and currently also on
 * transceivers that don't need any software attention to set up
 * the right sort of wakeup.
 * Also they depend on separate root hub suspend/resume.
 */

static int ehci_pci_resume(struct usb_hcd *hcd, bool hibernated)
{
	struct ehci_hcd		*ehci = hcd_to_ehci(hcd);
	struct pci_dev		*pdev = to_pci_dev(hcd->self.controller);

	if (ehci_resume(hcd, hibernated) != 0)
		(void) ehci_pci_reinit(ehci, pdev);
	return 0;
}

#else

#define ehci_suspend		NULL
#define ehci_pci_resume		NULL
#endif	/* CONFIG_PM */

static struct hc_driver __read_mostly ehci_smi_hc_driver;

static const struct ehci_driver_overrides pci_overrides = {
	.reset =		ehci_pci_setup,
};



/*-------------------------------------------------------------------------*/

int smi_ehci_init(struct drm_device *dev)
{
        struct hc_driver        *driver;
        struct usb_hcd          *hcd;
        int                     retval;
        int                     hcd_irq = 0;
		struct pci_dev *pdev = dev->pdev;
		struct smi_device *sdev = dev->dev_private;
		resource_size_t phymem;
		resource_size_t phymem_len;

		if (usb_disabled())
			return -ENODEV;
		
	
		ehci_init_driver(&ehci_smi_hc_driver, &pci_overrides);
		

#if LINUX_VERSION_CODE < KERNEL_VERSION(5,2,0)
		//For DMA function
		ehci_smi_hc_driver.flags |= HCD_LOCAL_MEM;
#endif
		
		/* Entries for the PCI suspend/resume callbacks are special */
		ehci_smi_hc_driver.pci_suspend = ehci_suspend;
		ehci_smi_hc_driver.pci_resume = ehci_pci_resume;

      
        driver = &ehci_smi_hc_driver;
		
        if (!driver)
                return -EINVAL;

	    pdev->dev.dma_mask = NULL;


    	  /*
         * The xHCI driver has its own irq management
         * make sure irq setup is not touched for xhci in generic hcd code
         */
        if ((driver->flags & HCD_MASK) < HCD_USB3) {
                if (!pdev->irq) {
                        dev_err(&pdev->dev,
                        "Found HC with no IRQ. Check BIOS/PCI %s setup!\n",
                                pci_name(pdev));
                        retval = -ENODEV;
                        goto disable_pci;
                }
                hcd_irq = pdev->irq;
        }


         phymem = sdev->mc.vram_base + 0x7f00000;
         phymem_len = 1 * 1024 * 1024;
#if 0
		/*
		 * Right now device-tree probed devices don't get dma_mask set.
		 * Since shared usb code relies on it, set it here for now.
		 * Once we have dma capability bindings this can go away.
		 */
		retval = dma_coerce_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
		if (retval)
			return retval;
#endif


#if LINUX_VERSION_CODE < KERNEL_VERSION(5,2,0)

	
/* The SM768 chip is equipped with local memory that may be used
 * by on-chip devices such as the video controller and the usb host.
 * This driver uses dma_declare_coherent_memory() to make sure
 * usb allocations with dma_alloc_coherent() allocate from
 * this local memory. The dma_handle returned by dma_alloc_coherent()
 * will be an offset starting from 0 for the first local memory byte.
 *
 * So as long as data is allocated using dma_alloc_coherent() all is
 * fine. This is however not always the case - buffers may be allocated
 * using kmalloc() - so the usb core needs to be told that it must copy
 * data into our local memory if the buffers happen to be placed in
 * regular memory. The HCD_LOCAL_MEM flag does just that.
 */

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,13,0)
	    if (!dma_declare_coherent_memory(&pdev->dev, phymem, phymem,
	         phymem_len, DMA_MEMORY_MAP |DMA_MEMORY_EXCLUSIVE)) {
	        dev_err(&pdev->dev, "cannot declare coherent memory\n");
	        retval = -ENXIO;
	        goto disable_pci;
	    }

#else
		retval = dma_declare_coherent_memory(&pdev->dev, phymem,
						 phymem,
#if LINUX_VERSION_CODE < KERNEL_VERSION(5,1,0)						 
						 phymem_len,
						 DMA_MEMORY_EXCLUSIVE);
#else
						 phymem_len);
#endif
		if (retval) {
			printk("cannot declare coherent memory\n");
			goto disable_pci;
		}
#endif
	 

#endif


        hcd = usb_create_hcd(driver, &pdev->dev, pci_name(pdev));
        if (!hcd) {
                retval = -ENOMEM;
        goto release_dma;
        }

        if (driver->flags & HCD_MEMORY) {
                /* EHCI, OHCI */
                hcd->rsrc_start = sdev->rmmio_base;
                hcd->rsrc_len = sdev->rmmio_size;
        hcd->regs =
            ioremap(hcd->rsrc_start, hcd->rsrc_len) + SMI_HOST_OFFSET;
                if (hcd->regs == NULL) {
                        printk("error mapping memory\n");
                        retval = -EFAULT;
                        goto release_mem_region;
                }

        } 

		pci_set_master(pdev);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,2,0)

     /* The SM768 chip is equipped with local memory that may be used
	 * by on-chip devices such as the video controller and the usb host.
	 * This driver uses genalloc so that usb allocations with
	 * gen_pool_dma_alloc() allocate from this local memory. The dma_handle
	 * returned by gen_pool_dma_alloc() will be an offset starting from 0
	 * for the first local memory byte.
	 *
	 * So as long as data is allocated using gen_pool_dma_alloc() all is
	 * fine. This is however not always the case - buffers may be allocated
	 * using kmalloc() - so the usb core needs to be told that it must copy
	 * regular memory. A non-null hcd->localmem_pool initialized by the
	 * the call to usb_hcd_setup_local_mem() below does just that.
	 */

	if (usb_hcd_setup_local_mem(hcd, phymem,
				    phymem,
				    phymem_len) < 0)
		goto unmap_registers;
		
#endif
	    //Enable USB Host intr and power
	    sb_IRQUnmask(SB_IRQ_VAL_USBH); 
		ddk768_enableUsbHost(1);
	
	    mdelay(500);

        /* Note: dev_set_drvdata must be called while holding the rwsem */
  
        down_write(&companions_rwsem);
        dev_set_drvdata(&pdev->dev, hcd);
        //for_each_companion(dev, hcd, ehci_pre_add);
        retval = usb_add_hcd(hcd, hcd_irq, IRQF_SHARED);
        if (retval != 0)
                dev_set_drvdata(&pdev->dev, NULL);
       // for_each_companion(dev, hcd, ehci_post_add);
        up_write(&companions_rwsem);

		
        if (retval != 0)
                goto unmap_registers;
        device_wakeup_enable(hcd->self.controller);

        if (pci_dev_run_wake(pdev))
                pm_runtime_put_noidle(&pdev->dev);
        return retval;

unmap_registers:
        if (driver->flags & HCD_MEMORY) {
		iounmap(hcd->regs - SMI_HOST_OFFSET);
release_mem_region:
                release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
        } else
                release_region(hcd->rsrc_start, hcd->rsrc_len);
		
        usb_put_hcd(hcd);
release_dma:

#if LINUX_VERSION_CODE < KERNEL_VERSION(5,2,0)
		dma_release_declared_memory(&pdev->dev);
#endif
disable_pci:
        pci_disable_device(pdev);
        dev_err(&pdev->dev, "init %s fail, %d\n", pci_name(pdev), retval);
        return retval;


}

void smi_ehci_remove(struct drm_device *dev)
{
	struct pci_dev *pdev = dev->pdev;
	struct smi_device *sdev = dev->dev_private;
		
	struct usb_hcd	*hcd = sdev->hcd;

	pci_clear_mwi(pdev);


	if (!hcd)
		return;

	if (pci_dev_run_wake(pdev))
		pm_runtime_get_noresume(&pdev->dev);

	/* Fake an interrupt request in order to give the driver a chance
	 * to test whether the controller hardware has been removed (e.g.,
	 * cardbus physical eject).
	 */
	local_irq_disable();
	usb_hcd_irq(0, hcd);
	local_irq_enable();

	/* Note: dev_set_drvdata must be called while holding the rwsem */

	down_write(&companions_rwsem);
//	for_each_companion(pdev, hcd, ehci_remove);
	usb_remove_hcd(hcd);
	dev_set_drvdata(&pdev->dev, NULL);
	up_write(&companions_rwsem);
#if LINUX_VERSION_CODE < KERNEL_VERSION(5,2,0)
	//release DMA
    dma_release_declared_memory(&pdev->dev);
#endif

	if (hcd->driver->flags & HCD_MEMORY) {
        iounmap(hcd->regs - SMI_HOST_OFFSET);
		release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
	} else {
		release_region(hcd->rsrc_start, hcd->rsrc_len);
	}

	usb_put_hcd(hcd);

}


/**
 * usb_hcd_pci_shutdown - shutdown host controller
 * @dev: USB Host Controller being shutdown
 */
void smi_ehci_shutdown(struct drm_device *dev)
{
		struct smi_device *sdev = dev->dev_private;
		
		struct usb_hcd	*hcd = sdev->hcd;

 
        if (!hcd)
                return;

        if (test_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags) &&
                        hcd->driver->shutdown) {
                hcd->driver->shutdown(hcd);
                if (usb_hcd_is_primary_hcd(hcd) && hcd->irq > 0)
                        free_irq(hcd->irq, hcd);
          
        }
}


