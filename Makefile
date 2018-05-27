worddocumentfrequency: worddocumentfrequency.c
	gcc worddocumentfrequency.c -fopenmp -std=gnu99 -DNUMCORES=4 -o worddocumentfrequency

clean:
	rm worddocumentfrequency -rf
