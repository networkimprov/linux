struct pcs_omap_pdata {
	int irq;
	void (*reconfigure_io_chain)(void);
};
