/**
 * struct pcs_reg - pinctrl register
 * @read:	pinctrl-single provided register read function
 * @write:	pinctrl-single provided register write function
 * @reg:	virtual address of a register
 * @val:	pinctrl configured value of the register
 * @irq:	optional irq specified for wake-up for example
 * @gpio:	optional gpio specified for wake-up for example
 * @node:	optional list
 */
struct pcs_reg {
	unsigned (*read)(void __iomem *reg);
	void (*write)(unsigned val, void __iomem *reg);
	void __iomem *reg;
	unsigned val;
	int irq;
	int gpio;
	struct list_head node;
};

#define PCS_HAS_FUNCTION_GPIO   (1 << 2)
#define PCS_HAS_FUNCTION_IRQ    (1 << 1)
#define PCS_HAS_PINCONF         (1 << 0)

/**
 * struct pcs_soc - SoC specific interface to pinctrl-single
 * @data:	SoC specific data pointer
 * @flags:	mask of PCS_HAS_xxx values
 * @reg_init:	SoC specific register init function
 * @enable:	SoC specific enable function
 * @disable:	SoC specific disable function
 */
struct pcs_soc {
	void *data;
	unsigned flags;
	int (*reg_init)(const struct pcs_soc *soc, struct pcs_reg *r);
	int (*enable)(const struct pcs_soc *soc, struct pcs_reg *r);
	void (*disable)(const struct pcs_soc *soc, struct pcs_reg *r);
};

extern int pinctrl_single_probe(struct platform_device *pdev,
				const struct pcs_soc *soc);
extern int pinctrl_single_remove(struct platform_device *pdev);
