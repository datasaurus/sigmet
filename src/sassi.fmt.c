    if ( snprintf(buf, LEN, "%02d%02d%02d_%02d%02d%02.0f_%s_%s_%.1f",
		yr, mo, da, h, mi, sec, vol_p->ih.ic.hw_site_name,
		abbrv, vol_p->sweep_angle[s] * DEG_PER_RAD) > LEN ) {
	memset(buf, 0, LEN);
	return 0;
    }
