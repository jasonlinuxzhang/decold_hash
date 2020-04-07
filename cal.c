#include "cal.h"

int64_t MIGRATION_COUNT = 0;
extern int enable_migration;
extern int enable_refs;
extern int enable_topk;
extern unsigned long int big_file;
extern float migration_threshold;


static int comp(const void *s1, const void *s2)
{
	return comp_code(((struct fp_info *)s1)->fp, ((struct fp_info *)s2)->fp);
}

static int comp_scommon(const void *s1, const void *s2)
{
	return ((struct fp_info *)s1)->fid - ((struct fp_info *)s2)->fid;
}

static int comp_mr(const void *mr1, const void *mr2)
{
	return ((struct file_info *)mr1)->fid - ((struct file_info *)mr2)->fid;
}


//we copy another S1 S2 for R1 and migartion sort
void cal_inter(GHashTable *g1_unique_chunks, GHashTable *g2_unique_chunks, struct fp_info *s1, int64_t s1_count, struct fp_info **scommon1, int64_t *sc1_count)
{
	int64_t i;

	//malloc for the scommon
	struct fp_info *scommon11 = (struct fp_info *)malloc(sizeof(struct fp_info) * s1_count);

	int64_t a = 0, b = 0;
	while (a < s1_count)
	{

		struct chunk * ck = g_hash_table_lookup(g1_unique_chunks, &s1[a].fp);
		if (NULL == ck) {
			printf("can't find ck in g1 hash table");
			exit(-1);
		}

		if (NULL == g_hash_table_lookup(g2_unique_chunks, &s1[a].fp)) {
			a++;
			continue;
		}

		memcpy(scommon11[*sc1_count].fp, s1[a + i].fp, sizeof(fingerprint));
		scommon11[*sc1_count].size = s1[a + i].size;

		scommon11[*sc1_count].cid = s1[a + i].cid;
		scommon11[*sc1_count].fid = s1[a + i].fid;
		scommon11[*sc1_count].order = s1[a + i].order;
		a++;
	}

	scommon11 = (struct fp_info *)realloc(scommon11, *sc1_count * sizeof(struct fp_info));

	*scommon1 = scommon11;
}

//check the full chunks in the S common
//0 none and migration < shreshold, <0 ->migration, >0 intersection
//if the bigger file, threshold smaller
static int64_t file_dedup_migration(struct file_info *mr, uint64_t count, uint64_t fid, uint64_t chunknum, int64_t mig_count[8], uint64_t *file_index)
{
	//binarySearch
	int64_t begin = 0, end = count - 1;
	while (begin <= end)
	{
		int64_t mid = (begin + end) / 2;
		if (mr[mid].fid == fid)
		{
			*file_index = mid;
			//TO-DO here for miagration!!!
			if (mr[mid].chunknum == chunknum)
				return mr[mid].chunknum; //>0 intersection
			else
			{
				//only big file migration
				if (enable_migration == 1)
				{
					double ratio = 1.0 * chunknum / mr[mid].chunknum;
					if (mr[mid].size >= big_file && ratio >= migration_threshold)
					{
						if (LEVEL >= 2)
							VERBOSE("BIG MIGRATION NO.%" PRId64 ", [%8" PRId64 "](size=%"PRId64"), { get [%" PRId64 "] of [%" PRId64 "] = %lf.} ==> MAY BE MIGRATION!\n", \
							MIGRATION_COUNT++, fid, mr[mid].size, chunknum, mr[mid].chunknum, ratio);

						if(ratio >=0.6)
						{
							mig_count[0]++;
							if(ratio >=0.65)
							{
								mig_count[1]++;
								if(ratio >=0.7)
								{
									mig_count[2]++;
									if(ratio >=0.75)
									{
										mig_count[3]++;
										if(ratio >=0.8)
										{
											mig_count[4]++;
											if(ratio >=0.85)
											{
												mig_count[5]++;
												if(ratio >=0.9)
												{
													mig_count[6]++;
													if(ratio >=0.95)
													{
														mig_count[7]++;
													}
												}
											}
										}
									}
								}
							}

						}

						return 0 - mr[mid].chunknum; //mark for migration
					}

				}

				return 0; //migration < shreshold
			}
		}
		else if (mr[mid].fid < fid)
			begin = mid + 1;
		else
			end = mid - 1;
	}

	return 0; //none
}



void file_find(struct file_info *mr, int64_t mr_count, struct fp_info *scommon, int64_t sc_count, struct identified_file_info **fff, int64_t *ff_count, struct migrated_file_info **mm, int64_t *m_count, int64_t mig_count[8])
{
	int64_t i;

	struct identified_file_info *ff = (struct identified_file_info *)malloc(sc_count * sizeof(struct identified_file_info));
	struct migrated_file_info *m = (struct migrated_file_info *)malloc(sc_count * sizeof(struct migrated_file_info));

	for (i = 0; i < sc_count;)
	{
		int64_t j = 1;
		while (i + j < sc_count && scommon[i].fid == scommon[i + j].fid)
			j++;

		struct file_info *fi = scommon[i].fi;
	
		if (fi->chunknum == j) {
			//put the files in the common file
			ff[*ff_count].fid = fi->fid;
			ff[*ff_count].num = j;
			ff[*ff_count].fps = (fingerprint *)malloc(j * sizeof(fingerprint));
			ff[*ff_count].sizes = (int32_t *)malloc(j * sizeof(int64_t));

			int64_t s;
			for (s = 0; s < j; s++)
			{
				int64_t order = scommon[i + s].order;
				memcpy((ff[*ff_count].fps)[order], scommon[i + s].fp, sizeof(fingerprint));
				(ff[*ff_count].sizes)[order] = scommon[i + s].size;
			}
			(*ff_count)++;
		} else if (fi->size >= big_file && (1.0 * j / fi->chunknum) >= migration_threshold) {
			m[*m_count].fid = fi->fid;
			m[*m_count].total_num = fi->chunknum;
			m[*m_count].fps = (fingerprint *)malloc(fi->chunknum * sizeof(fingerprint));
			m[*m_count].arr = (uint64_t *)calloc(1, 2 * fi->chunknum * sizeof(int64_t));
			m[*m_count].fp_cids = (int64_t *)calloc(1, fi->chunknum * sizeof(containerid));
			m[*m_count].fp_info_start = fi->fp_info_start;

			memset(m[*m_count].arr, 0, 2 * fi->chunknum * sizeof(int64_t));
			memset(m[*m_count].fp_cids, 0, fi->chunknum * sizeof(containerid));

			int64_t s;
			for (s = 0; s < j; s++)
			{
				//fps are sorted by order
				int64_t order = scommon[i + s].order;

				memcpy((m[*m_count].fps)[order], scommon[i + s].fp, sizeof(fingerprint));

				(m[*m_count].arr)[order] = scommon[i + s].size;
				//in ==1, mean it in the scommon
				(m[*m_count].arr)[order + fi->chunknum] = 1;
				//cid
				(m[*m_count].fp_cids)[order] = scommon[i + s].cid;
			}
			(*m_count)++;
		}
		
		i = i + j;
	}

	ff = (struct identified_file_info *)realloc(ff, *ff_count * sizeof(struct identified_file_info));
	*fff = ff;

	if (enable_migration == 1)
	{
		m = (struct migrated_file_info *)realloc(m, *m_count * sizeof(struct migrated_file_info));
		*mm = m;
	}
}

