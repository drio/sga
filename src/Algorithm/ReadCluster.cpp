///-----------------------------------------------
// Copyright 2011 Wellcome Trust Sanger Institute
// Written by Jared Simpson (js18@sanger.ac.uk)
// Released under the GPL
//-----------------------------------------------
//
// ReadCluster - Generate a cluster of overlapping
// reads using the FM-index
//
#include "ReadCluster.h"

//
ReadCluster::ReadCluster(const OverlapAlgorithm* pOverlapper, int minOverlap) : m_pOverlapper(pOverlapper), 
                                                                                m_minOverlap(minOverlap)
{

}

// Add a seed read to the cluster. Overlaps will be found for 
// each seed read to grow the cluster
ClusterNode ReadCluster::addSeed(const std::string& sequence, bool bCheckInIndex)
{
    // Check if this read is a substring
    SeqRecord tempRecord;
    tempRecord.id = "cluster-seed";
    tempRecord.seq = sequence;

    OverlapBlockList tempBlockList;
    OverlapResult overlapResult = m_pOverlapper->alignReadDuplicate(tempRecord, &tempBlockList);
    if(overlapResult.isSubstring)
    {
        // If bCheckInIndex is true, then we are extending clusters from reads that are in the FM-index
        // In this case, seeding a substring read is an error and we abort. If the seed is NOT in the index
        // we just emit a warning and ignore the seed
        if(bCheckInIndex)
        {
            std::cerr << "Error: The seed used for sga-cluster-extend is a substring of some read.\n";
            std::cerr << "Please run sga rmdup before cluster\n";
            std::cerr << "Sequence: " << sequence << "\n";
            exit(1);
        }
        else
        {
            std::cerr << "Warning: The cluster sequence to extend is a substring of some read. This seed is being skipped\n";
            std::cerr << "Sequence: " << sequence << "\n";
            ClusterNode emptyNode;

            // Set an invalid interval
            emptyNode.interval.lower = 0;
            emptyNode.interval.upper = -1;
            return emptyNode;
        }
    }

    // Find the interval in the fm-index containing the read
    const BWT* pBWT = m_pOverlapper->getBWT();
    BWTInterval readInterval = BWTAlgorithms::findInterval(pBWT, sequence);
    BWTAlgorithms::updateInterval(readInterval, '$', pBWT);

    // When building primary clusters, we require each read to be in the index.
    if(bCheckInIndex)
    {
        if(!readInterval.isValid())
        {
            std::cerr << "sga cluster error: The seed read is not part of the FM-index.\n";
            exit(EXIT_FAILURE);
        }
    }

    ClusterNode node;
    node.sequence = sequence;
    node.interval = readInterval;
    node.isReverseInterval = false;
    m_usedIndex.insert(readInterval.lower);
    m_queue.push(node);
    return node;
}

// Run the cluster process. If the number of total nodes
// exceeds max, abort the search.
void ReadCluster::run(size_t max)
{
    while(!m_queue.empty())
    {
        if(m_queue.size() + m_outCluster.size() > max)
        {
            while(!m_queue.empty())
                m_queue.pop();
            m_outCluster.clear();
            return;
        }

        ClusterNode node = m_queue.front();
        m_queue.pop();

        // Add this node to the output
        m_outCluster.push_back(node);

        // Find overlaps for the current node
        SeqRecord tempRecord;
        tempRecord.id = "cluster";
        tempRecord.seq = node.sequence;
        OverlapBlockList blockList;
        m_pOverlapper->overlapRead(tempRecord, m_minOverlap, &blockList);
        
        // Parse each member of the block list and potentially expand the cluster
        for(OverlapBlockList::const_iterator iter = blockList.begin(); iter != blockList.end(); ++iter)
        {
            // Check if the reads in this block are part of the cluster already
            BWTInterval canonicalInterval = iter->getCanonicalInterval();
            int64_t canonicalIndex = canonicalInterval.lower;
            if(m_usedIndex.count(canonicalIndex) == 0)
            {
                // This is a new node that isn't in the cluster. Add it.
                m_usedIndex.insert(canonicalIndex);

                ClusterNode newNode;
                newNode.sequence = iter->getFullString(node.sequence);
                newNode.interval = canonicalInterval;
                newNode.isReverseInterval = iter->flags.isTargetRev();
                m_queue.push(newNode);
            }
        }
    }
}

// 
ClusterNodeVector ReadCluster::getOutput() const
{
    // Sort the intervals into ascending order and remove any duplicate intervals (which can occur
    // if the subgraph has a simple cycle)
    ClusterNodeVector retVector = m_outCluster;
    std::sort(retVector.begin(), retVector.end(), ClusterNode::compare);

    std::vector<ClusterNode>::iterator newEnd = std::unique(retVector.begin(),
                                                            retVector.end(),
                                                            ClusterNode::equal);

    retVector.erase(newEnd, retVector.end());
    return retVector;
}
