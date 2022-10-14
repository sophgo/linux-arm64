#ifndef __CE_BASE_H__
#define __CE_BASE_H__

int register_all_base(struct device *dev);
int unregister_all_base(struct device *dev);
void ce_base_handle_request(struct ce_inst *inst,
			    struct crypto_async_request *req);
static inline int ce_base_is_base_request(struct crypto_async_request *creq)
{
	return creq->tfm == NULL;
}

#endif
