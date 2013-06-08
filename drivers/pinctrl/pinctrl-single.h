#define PCS_HAS_PINCONF         (1 << 0)

/**
 * struct pcs_soc - SoC specific interface to pinctrl-single
 * @data:	SoC specific data pointer
 * @flags:	mask of PCS_HAS_xxx values
 */
struct pcs_soc {
	void *data;
	unsigned flags;
};

extern int pinctrl_single_probe(struct platform_device *pdev,
				const struct pcs_soc *soc);
extern int pinctrl_single_remove(struct platform_device *pdev);
