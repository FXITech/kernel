#ifndef fxi_id_h
#define fxi_id_h

#define FXI_VENDOR_ID 0x274D

struct fxi_product_info {
	u16 idVendor;
	u16 idProduct;
	char iManufacturer[80];
	char iProduct[80];
	char iSerial[80];
};

void fxi_get_product_info(struct fxi_product_info* info);

#endif

