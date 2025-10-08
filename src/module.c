struct module {
	const char *id;
	void *handle;
	int (*init)(void);
	int (*shutdown)(void);
};
