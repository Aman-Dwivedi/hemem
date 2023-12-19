awk '{ printf("0x%x\n", $1 / 8); }' indices1.txt >| cachelines1.txt
awk '{ printf("0x%x\n", $1 / 8); }' indices2.txt >| cachelines2.txt
awk '{ printf("0x%x\n", $1 / 8); }' indices3.txt >| cachelines3.txt
sort cachelines1.txt | uniq > uniq1.txt
sort cachelines2.txt | uniq > uniq2.txt
sort cachelines3.txt | uniq > uniq3.txt
comm -12 uniq1.txt uniq2.txt | gawk -n '$1 < 0x40000000 { print }' | wc -l
comm -12 uniq2.txt uniq3.txt | gawk -n '$1 < 0x40000000 { print }' | wc -l
comm -12 uniq1.txt uniq3.txt | gawk -n '$1 < 0x40000000 { print }' | wc -l
awk '{ printf("0x%x\n", $1 / 262144); }' indices1.txt >| pages1.txt
awk '{ printf("0x%x\n", $1 / 262144); }' indices3.txt >| pages3.txt
sort pages1.txt | uniq > uniq-pages1.txt
sort pages3.txt | uniq > uniq-pages3.txt
comm -12 uniq-pages1.txt uniq-pages3.txt | gawk -n '$1 < 0x40000000 { print }' | wc -l

